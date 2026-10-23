#include "PinyinEngines.h"

#include <cstddef>
#include <cstdint>
#include <system_error>
#include <string>
#include <utility>

#include <cpp-pinyin2/core/Engine.h>
#include <cpp-pinyin2/core/Types.h>
#include <cpp-pinyin2/ext/Registry.h>

#include <stdcorelib/path.h>

namespace fs = std::filesystem;

namespace wolf::pinyin {

    namespace {

        constexpr char MANDARIN[] = "mandarin";
        constexpr char CANTONESE[] = "cantonese";

        /// The registry of the built-in dialects, which the 2.x library keeps in the caller.
        ///
        /// A function-local static, so that the engines opened against it outlive every static that
        /// could still call into them during shutdown. `Registry` is neither copyable nor movable.
        cpp_pinyin::v2::Registry &registry() {
            static cpp_pinyin::v2::Registry instance;
            return instance;
        }

        /// Returns the number of UTF-16 code units of \a text, counting one per code point and two
        /// for every code point above U+FFFF. The 2.x library refuses a whole batch when one element
        /// is not exactly one code unit, and a 4-byte UTF-8 sequence is two of them.
        std::size_t utf16CodeUnits(std::string_view text) {
            std::size_t units = 0;
            for (const char byte : text) {
                const auto lead = static_cast<unsigned char>(byte);
                // Count only the bytes that start a sequence; continuation bytes are skipped.
                if ((lead & 0xC0) != 0x80) {
                    units += lead >= 0xF0 ? 2 : 1;
                }
            }
            return units;
        }

        /// One cpp-pinyin engine plus the dialect it was opened for.
        class ConvertibleEngine final : public Engine {
        public:
            ConvertibleEngine(cpp_pinyin::v2::Engine engine, bool mandarin)
                : m_engine(std::move(engine)), m_mandarin(mandarin) {
            }

            std::vector<CharacterResult>
                convert(const std::vector<std::string> &characters) const override {
                std::vector<CharacterResult> results(characters.size());

                // The library answers the whole batch with one error when any element is not
                // exactly one UTF-16 code unit. Characters outside the Basic Multilingual Plane
                // (CJK extension B names, emoji) are two units, so they are held back here and
                // answered as unconverted, which lets the chain fall back for that character
                // alone instead of losing the run.
                std::vector<std::string> convertible;
                std::vector<std::size_t> positions;
                convertible.reserve(characters.size());
                positions.reserve(characters.size());
                for (std::size_t i = 0; i < characters.size(); ++i) {
                    if (utf16CodeUnits(characters[i]) == 1) {
                        convertible.push_back(characters[i]);
                        positions.push_back(i);
                    }
                }
                if (convertible.empty()) {
                    return results;
                }

                cpp_pinyin::v2::EngineConfig config;
                config.style = m_mandarin ? cpp_pinyin::v2::Style(cpp_pinyin::v2::MandarinStyle::NORMAL)
                                          : cpp_pinyin::v2::Style(cpp_pinyin::v2::CantoneseStyle::NORMAL);

                auto queried = m_engine.query(convertible, config);
                if (!queried) {
                    // A batch-level failure (the engine is not ready, the dictionaries changed under
                    // it) is reported as "nothing converted" so that the chain falls back. The
                    // alternative -- failing the whole word -- would turn one bad character into a
                    // failed conversion of the line.
                    return results;
                }
                const auto &rows = queried.value();
                if (rows.size() != convertible.size()) {
                    // The library documents one row per element. A different count cannot be mapped
                    // back onto the input, so the run is left unconverted rather than misaligned.
                    return results;
                }

                for (std::size_t i = 0; i < rows.size(); ++i) {
                    const auto &row = rows[i];
                    if (row.failure_kind != cpp_pinyin::v2::Code::Ok) {
                        continue;
                    }
                    auto &result = results[positions[i]];
                    result.pronunciation = row.rendered;
                    result.candidates = row.candidates;
                    if (result.candidates.empty() && !result.pronunciation.empty()) {
                        result.candidates.push_back(result.pronunciation);
                    }
                }
                return results;
            }

        private:
            cpp_pinyin::v2::Engine m_engine;
            bool m_mandarin;
        };

    }

    Engine::Engine() = default;

    Engine::~Engine() = default;

    bool PinyinEngineRegistry::isKnownRef(std::string_view ref) {
        return ref == MANDARIN || ref == CANTONESE;
    }

    srt::Expected<std::unique_ptr<Engine>>
        PinyinEngineRegistry::createEngine(const fs::path &root, std::string_view ref) {
        if (!isKnownRef(ref)) {
            return srt::Error(srt::Error::FeatureNotSupported,
                              "unknown pinyin engine: " + std::string(ref));
        }

        // The dialect directory is checked before the library is called so that the diagnostic names
        // the directory the declaration asked for; the library reports the files it could not read,
        // which is a level lower than the author needs.
        const auto directory = root / std::string(ref);
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

        auto opened = cpp_pinyin::v2::Engine::open(registry(), root, ref);
        if (!opened) {
            const auto &error = opened.error();
            if (error.code == cpp_pinyin::v2::Code::NotReady) {
                return srt::Error(srt::Error::FeatureNotSupported, error.detail);
            }
            return srt::Error(srt::Error::InvalidFormat, error.detail);
        }

        return std::unique_ptr<Engine>(
            new ConvertibleEngine(std::move(opened).value(), ref == MANDARIN));
    }

}
