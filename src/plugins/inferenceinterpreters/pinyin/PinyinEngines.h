#ifndef WOLF_PINYINENGINES_H
#define WOLF_PINYINENGINES_H

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <synthrt/Support/Expected.h>

namespace wolf::pinyin {

    /// What the engine produced for one character.
    struct CharacterResult {
        /// Empty when the engine has no reading for the character.
        std::string pronunciation;

        /// Readings of a polyphonic character, the first of which is the main one.
        std::vector<std::string> candidates;
    };

    /// One cpp-pinyin engine bound to one subdirectory of the dictionary root.
    ///
    /// An engine is not safe to use from two threads at once: the upstream conversion methods are
    /// const but write scratch buffers held by the instance. Every executive therefore owns one,
    /// which is also why these are not shared through the resource cache.
    class Engine {
    public:
        virtual ~Engine();

        /// Converts a run of adjacent characters.
        ///
        /// Adjacency matters: the engine reads its phrase tables across neighbours, so a caller
        /// that hands over characters one at a time loses every phrase level disambiguation.
        /// Returns one entry per input character, in order.
        virtual std::vector<CharacterResult>
            convert(const std::vector<std::string> &characters) const = 0;

    protected:
        Engine();
    };

    /// The shared arbiter state. Defined in the translation unit; held through a shared pointer so
    /// that a reservation outliving the registry singleton at static destruction stays well
    /// defined.
    class RootState;

    /// A live claim on the process wide dictionary root, released when it is destroyed.
    ///
    /// This is what makes the claim a transaction private state change in the sense the upper
    /// specification requires of Acquire: the claim rides on the configuration object that took
    /// it, so a load that fails afterwards drops the configuration and the claim with it, and a
    /// package that is unloaded returns the root instead of holding it for the life of the
    /// process.
    class RootReservation {
    public:
        RootReservation() = default;
        ~RootReservation();

        RootReservation(RootReservation &&other) noexcept;
        RootReservation &operator=(RootReservation &&other) noexcept;

        RootReservation(const RootReservation &) = delete;
        RootReservation &operator=(const RootReservation &) = delete;

        /// Whether this object holds a claim. A default constructed one does not.
        bool held() const noexcept;

    private:
        friend class PinyinEngineRegistry;

        explicit RootReservation(std::shared_ptr<RootState> state);

        std::shared_ptr<RootState> m_state;
    };

    /// Owns the one dictionary root the process may have.
    ///
    /// cpp-pinyin resolves dictionaries through a process global path that its engine constructor
    /// reads. Two modules shipping different roots would therefore silently disable one another,
    /// which is what this arbiter exists to prevent: the first root wins, an identical root is
    /// accepted, and a second, different root fails the package that carries it while naming both.
    ///
    /// Reservation happens while the configuration is being read, so the conflict surfaces as a
    /// load failure rather than as an engine that quietly converts nothing.
    class PinyinEngineRegistry {
    public:
        static PinyinEngineRegistry &instance();

        /// Claims \a root as the dictionary root of this process, for as long as the returned
        /// reservation lives.
        ///
        /// Claims nest: a second module naming the same root gets its own reservation, and the
        /// root is forgotten only when the last one goes. An earlier design claimed the root
        /// permanently, so a load that failed after claiming it disabled every other root in the
        /// process for good.
        srt::Expected<RootReservation> reserveRoot(const std::filesystem::path &root);

        /// Builds an engine for \a ref, one of the refs this build knows.
        ///
        /// Safe to call concurrently. The lock covers only the one time the reserved root is
        /// handed to cpp-pinyin's process global path; the constructor that reads that path builds
        /// outside it, so the two languages of one voicebank warm in parallel.
        srt::Expected<std::unique_ptr<Engine>> createEngine(const std::filesystem::path &root,
                                                            std::string_view ref);

        /// Whether \a ref names an engine this build carries.
        static bool isKnownRef(std::string_view ref);

        /// Forgets the reserved root and every outstanding claim on it. Test scaffolding.
        void reset();

    private:
        PinyinEngineRegistry();
        ~PinyinEngineRegistry();

        std::shared_ptr<RootState> _impl;
    };

}

#endif // WOLF_PINYINENGINES_H
