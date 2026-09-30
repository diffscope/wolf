#ifndef WOLF_PINYINENGINES_H
#define WOLF_PINYINENGINES_H

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <synthrt/Support/Expected.h>

namespace wolf::pinyin {

    /// Conversion result of one character.
    struct CharacterResult {
        /// Empty if the engine has no reading for the character.
        std::string pronunciation;

        /// Readings of a polyphonic character. The first reading is the primary reading.
        std::vector<std::string> candidates;
    };

    /// One cpp-pinyin engine bound to one subdirectory of the dictionary root.
    ///
    /// An engine is not thread-safe: the upstream conversion methods are const but write scratch
    /// buffers held by the instance. Every executive therefore owns its own engine, and engines are
    /// not shared through the resource cache.
    class Engine {
    public:
        virtual ~Engine();

        /// Converts a run of adjacent characters.
        ///
        /// The engine matches its phrase tables across adjacent characters, so a caller that
        /// passes characters one at a time loses all phrase-level disambiguation.
        ///
        /// \return One entry per input character, in input order.
        virtual std::vector<CharacterResult>
            convert(const std::vector<std::string> &characters) const = 0;

    protected:
        Engine();
    };

    /// Shared arbiter state, defined in the translation unit. The state is held through a shared
    /// pointer so that a reservation that outlives the registry singleton during static
    /// destruction remains well-defined.
    class RootState;

    /// Live claim on the process-wide dictionary root, released on destruction.
    ///
    /// The reservation makes the claim a transaction-private state change, as the higher-level
    /// specification requires of Acquire. The configuration object that acquired the claim owns
    /// the reservation, so a load that fails afterwards destroys the configuration together with
    /// the claim, and an unloaded package releases the root instead of holding it for the lifetime
    /// of the process.
    class RootReservation {
    public:
        RootReservation() = default;
        ~RootReservation();

        RootReservation(RootReservation &&other) noexcept;
        RootReservation &operator=(RootReservation &&other) noexcept;

        RootReservation(const RootReservation &) = delete;
        RootReservation &operator=(const RootReservation &) = delete;

        /// Returns whether this object holds a claim. A default-constructed reservation holds no
        /// claim.
        bool held() const noexcept;

    private:
        friend class PinyinEngineRegistry;

        explicit RootReservation(std::shared_ptr<RootState> state);

        std::shared_ptr<RootState> m_state;
    };

    /// Owns the single dictionary root that the process may use.
    ///
    /// cpp-pinyin resolves dictionaries through a process-global path that its engine constructor
    /// reads. Two modules that ship different roots would therefore silently disable each other.
    /// This arbiter prevents that conflict: the first root is reserved, an identical root is
    /// accepted, and a different root fails the package that declares it with a diagnostic that
    /// names both roots.
    ///
    /// The root is reserved while the configuration is read, so the conflict appears as a load
    /// failure rather than as an engine that silently converts nothing.
    class PinyinEngineRegistry {
    public:
        static PinyinEngineRegistry &instance();

        /// Claims \a root as the dictionary root of this process for the lifetime of the returned
        /// reservation.
        ///
        /// Claims nest: a second module that names the same root receives its own reservation,
        /// and the root is released only when the last reservation is destroyed. An earlier
        /// design claimed the root permanently, so a load that failed after the claim disabled
        /// every other root for the remaining lifetime of the process.
        ///
        /// \return A reservation if no root is reserved or if \a root equals the reserved root;
        /// otherwise a FeatureNotSupported error that names both roots.
        srt::Expected<RootReservation> reserveRoot(const std::filesystem::path &root);

        /// Builds an engine for \a ref, which must be a ref supported by this build.
        ///
        /// This function is thread-safe. The lock covers only the single assignment of the
        /// reserved root to the process-global path of cpp-pinyin. The constructor that reads that
        /// path runs outside the lock, so the two languages of one voicebank initialize in
        /// parallel.
        ///
        /// \return The engine. A FileNotFound error if the dictionary directory is missing or
        /// empty. An InvalidFormat error if the dictionary cannot be read. A FeatureNotSupported
        /// error if \a root is not the reserved root or \a ref is unknown.
        srt::Expected<std::unique_ptr<Engine>> createEngine(const std::filesystem::path &root,
                                                            std::string_view ref);

        /// Returns whether \a ref names an engine that this build includes.
        static bool isKnownRef(std::string_view ref);

        /// Clears the reserved root and every outstanding claim on it. Intended for tests only.
        void reset();

    private:
        PinyinEngineRegistry();
        ~PinyinEngineRegistry();

        std::shared_ptr<RootState> _impl;
    };

}

#endif // WOLF_PINYINENGINES_H
