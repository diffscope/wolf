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

    /// One cpp-pinyin engine bound to one dialect and one dictionary root.
    ///
    /// The 2.x library has no process-global dictionary path: an engine is opened against the root
    /// it is given and owns that association. Two engines of one process may therefore read two
    /// different roots, which the 1.x line could not do.
    ///
    /// An engine is not thread-safe for the mutating calls, but every conversion below is
    /// read-only: after construction the upstream library documents `query` as safe on a shared
    /// instance. Each executive still owns its own engine, because the configuration of a module
    /// carries the root and the two languages of one voicebank must not serialize.
    class Engine {
    public:
        virtual ~Engine();

        /// Converts a run of adjacent characters.
        ///
        /// The engine matches its phrase tables across adjacent characters, so a caller that
        /// passes characters one at a time loses all phrase-level disambiguation.
        ///
        /// Characters the library cannot accept as one element -- anything outside the Basic
        /// Multilingual Plane becomes a surrogate pair in its UTF-16 view -- are skipped and
        /// answered with an empty result, so that one rare character cannot fail the whole run.
        ///
        /// \return One entry per input character, in input order.
        virtual std::vector<CharacterResult>
            convert(const std::vector<std::string> &characters) const = 0;

    protected:
        Engine();
    };

    /// Builds engines of the cpp-pinyin 2.x line.
    ///
    /// This class used to be an arbiter of the process-wide dictionary root: the 1.x library kept
    /// that root in a global that its engine constructor read, so two roots in one process silently
    /// disabled each other and the first one was reserved for the lifetime of a configuration. The
    /// 2.x library takes the root as an argument to `open`, so the arbiter has nothing left to
    /// arbitrate and is gone.
    class PinyinEngineRegistry {
    public:
        /// Returns whether \a ref names an engine that this build includes.
        static bool isKnownRef(std::string_view ref);

        /// Builds an engine for \a ref against \a root.
        ///
        /// \return The engine. A FileNotFound error if the dialect directory is missing or empty.
        /// An InvalidFormat error if the dictionary cannot be read. A FeatureNotSupported error if
        /// \a ref is unknown.
        static srt::Expected<std::unique_ptr<Engine>>
            createEngine(const std::filesystem::path &root, std::string_view ref);
    };

}

#endif // WOLF_PINYINENGINES_H
