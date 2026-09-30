#include "PinyinEngines.h"

#include <cstddef>
#include <mutex>
#include <system_error>
#include <utility>

#include <cpp-pinyin/G2pglobal.h>
#include <cpp-pinyin/Jyutping.h>
#include <cpp-pinyin/Pinyin.h>

#include <stdcorelib/path.h>

#include <wolf/Support/ManifestValues.h>

namespace fs = std::filesystem;

namespace wolf::pinyin {

    namespace {

        constexpr char MANDARIN[] = "mandarin";
        constexpr char CANTONESE[] = "cantonese";

        /// Converts one upstream batch into the result type of this plugin.
        ///
        /// The upstream library reports a failed character by keeping the character itself as the
        /// reading and setting its error flag. Returning that reading would contradict the
        /// contract, under which a word with an error has no defined pronunciation. An unconverted
        /// character is therefore returned empty, and the fallback of the chain handles it.
        ///
        /// \return One entry per upstream item, in order.
        std::vector<CharacterResult> collect(const Pinyin::PinyinResVector &converted) {
            std::vector<CharacterResult> results;
            results.reserve(converted.size());
            for (const auto &item : converted) {
                CharacterResult result;
                if (!item.error) {
                    result.pronunciation = item.pinyin;
                    result.candidates = item.candidates;
                    if (result.candidates.empty() && !result.pronunciation.empty()) {
                        result.candidates.push_back(result.pronunciation);
                    }
                }
                results.push_back(std::move(result));
            }
            return results;
        }

        /// Both engines convert without tone marks, as the DiffSinger phoneme sets require.
        class MandarinEngine : public Engine {
        public:
            bool initialized() const {
                return m_engine.initialized();
            }

            std::vector<CharacterResult>
                convert(const std::vector<std::string> &characters) const override {
                return collect(m_engine.hanziToPinyin(characters, Pinyin::ManTone::NORMAL,
                                                      Pinyin::Default, true, false, false));
            }

        private:
            Pinyin::Pinyin m_engine;
        };

        class CantoneseEngine : public Engine {
        public:
            bool initialized() const {
                return m_engine.initialized();
            }

            std::vector<CharacterResult>
                convert(const std::vector<std::string> &characters) const override {
                return collect(m_engine.hanziToPinyin(characters, Pinyin::CanTone::NORMAL,
                                                      Pinyin::Default, true));
            }

        private:
            Pinyin::Jyutping m_engine;
        };

    }

    Engine::Engine() = default;

    Engine::~Engine() = default;

    /// Arbiter state shared by the registry and every live reservation.
    class RootState {
    public:
        std::mutex mutex;
        fs::path root;

        /// Number of live reservations on \c root. The root is cleared when this count reaches
        /// zero, so a failed load or an unload releases the claim instead of holding it
        /// permanently.
        std::size_t claims = 0;

        /// Whether \c root has been assigned to the process-global path of cpp-pinyin.
        ///
        /// The root is published exactly once per claim generation. Writing it again while another
        /// thread is inside an engine constructor would be a data race on that global even if the
        /// value were identical, because assigning a std::filesystem::path is not atomic.
        bool published = false;
    };

    RootReservation::RootReservation(std::shared_ptr<RootState> state)
        : m_state(std::move(state)) {
    }

    RootReservation::~RootReservation() {
        if (!m_state) {
            return;
        }
        std::lock_guard<std::mutex> guard(m_state->mutex);
        // Guards against a reset() that has already cleared the claims while this object exists.
        if (m_state->claims > 0 && --m_state->claims == 0) {
            m_state->root.clear();
            m_state->published = false;
        }
    }

    RootReservation::RootReservation(RootReservation &&other) noexcept = default;

    RootReservation &RootReservation::operator=(RootReservation &&other) noexcept {
        if (this != &other) {
            RootReservation released(std::move(*this));
            m_state = std::move(other.m_state);
        }
        return *this;
    }

    bool RootReservation::held() const noexcept {
        return static_cast<bool>(m_state);
    }

    PinyinEngineRegistry::PinyinEngineRegistry() : _impl(std::make_shared<RootState>()) {
    }

    PinyinEngineRegistry::~PinyinEngineRegistry() = default;

    PinyinEngineRegistry &PinyinEngineRegistry::instance() {
        static PinyinEngineRegistry registry;
        return registry;
    }

    bool PinyinEngineRegistry::isKnownRef(std::string_view ref) {
        return ref == MANDARIN || ref == CANTONESE;
    }

    srt::Expected<RootReservation> PinyinEngineRegistry::reserveRoot(const fs::path &root) {
        std::lock_guard<std::mutex> guard(_impl->mutex);
        if (_impl->claims == 0) {
            _impl->root = root;
            _impl->claims = 1;
            _impl->published = false;
            return RootReservation(_impl);
        }
        if (_impl->root == root) {
            ++_impl->claims;
            return RootReservation(_impl);
        }
        return srt::Error(srt::Error::FeatureNotSupported,
                          "a pinyin dictionary root is already in use by this process: " +
                              stdc::path::to_utf8(_impl->root) +
                              "; only one root may be loaded at a time, and this module "
                              "specifies " +
                              stdc::path::to_utf8(root));
    }

    srt::Expected<std::unique_ptr<Engine>> PinyinEngineRegistry::createEngine(const fs::path &root,
                                                                             std::string_view ref) {
        // The upstream library loops over the dictionary files without checking that they exist,
        // so a missing directory causes the constructor to loop indefinitely instead of returning.
        // The directory is therefore checked before construction. This check does not access
        // shared state, so it runs outside the lock.
        const auto directory = root / pathFromManifest(ref);
        std::error_code ec;
        if (!fs::is_directory(directory, ec)) {
            return srt::Error(srt::Error::FileNotFound,
                              "the pinyin dictionary directory is missing: " +
                                  stdc::path::to_utf8(directory));
        }
        if (fs::directory_iterator(directory, ec) == fs::directory_iterator() || ec) {
            return srt::Error(srt::Error::FileNotFound,
                              "the pinyin dictionary directory is empty: " +
                                  stdc::path::to_utf8(directory));
        }

        // The lock covers only the publication of the process-global path of cpp-pinyin, once per
        // claim generation. The path cannot be written again while an engine is being built:
        // building an engine requires a live claim, and the root is cleared and republished only
        // after the last claim has been released.
        {
            std::lock_guard<std::mutex> guard(_impl->mutex);
            if (_impl->root != root) {
                return srt::Error(srt::Error::FeatureNotSupported,
                                  "the pinyin dictionary root changed after it was reserved");
            }
            if (!_impl->published) {
                Pinyin::setDictionaryPath(root);
                _impl->published = true;
            }
        }

        // Construction runs outside the lock, so two languages of one voicebank initialize in
        // parallel instead of sequentially. This property has been verified against the upstream
        // sources: ChineseG2pPrivate::init() reads the global path once and writes only
        // per-instance members, and every other static in that library is `static const`, which
        // C++ initializes exactly once in its function-local form.
        if (ref == MANDARIN) {
            auto engine = std::make_unique<MandarinEngine>();
            if (!engine->initialized()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "the mandarin dictionary could not be read from " +
                                      stdc::path::to_utf8(directory));
            }
            return std::unique_ptr<Engine>(std::move(engine));
        }
        if (ref == CANTONESE) {
            auto engine = std::make_unique<CantoneseEngine>();
            if (!engine->initialized()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "the cantonese dictionary could not be read from " +
                                      stdc::path::to_utf8(directory));
            }
            return std::unique_ptr<Engine>(std::move(engine));
        }
        return srt::Error(srt::Error::FeatureNotSupported,
                          "unknown pinyin engine: " + std::string(ref));
    }

    void PinyinEngineRegistry::reset() {
        std::lock_guard<std::mutex> guard(_impl->mutex);
        _impl->root.clear();
        _impl->claims = 0;
        _impl->published = false;
    }

}
