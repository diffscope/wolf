#ifndef WOLF_ONSETRULES_H
#define WOLF_ONSETRULES_H

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <synthrt/Support/Expected.h>

namespace wolf::onset {

    /// Marks onset positions by matching phoneme patterns.
    ///
    /// A pattern segment is a literal phoneme, the name of a registered phoneme type, or the
    /// wildcard `*`. Matching runs left to right: at each position the best rule wins and the scan
    /// advances past it; a position no rule covers stays false, which is the contract's meaning
    /// rather than an error.
    ///
    /// Best means longest, then the most literal segments, then the most typed ones, then the
    /// earliest literal. Literal beating typed is what makes a specific phoneme override the rule
    /// written for its whole class.
    class RuleTable {
    public:
        /// The rule file shape this build reads.
        ///
        /// The field is optional: a file naming no version is read as version 1, which is the shape
        /// every rule file shipped so far has. A file naming a higher version is refused rather
        /// than guessed at.
        ///
        /// Changing the shape therefore means raising this constant and the cache generation
        /// `RULE_KIND` together: the version refuses the file, the generation keeps the previous
        /// parse out of the cache.
        static constexpr int FORMAT_VERSION = 1;

        static srt::Expected<RuleTable> load(const std::filesystem::path &path);

        std::vector<bool> mark(const std::vector<std::string> &phonemes) const;

    private:
        struct Rule {
            std::vector<std::string> pattern;
            std::vector<std::size_t> onsets;
        };

        /// How well one rule fits at a position, or nothing when it does not fit.
        struct Fit {
            std::size_t length = 0;
            std::size_t exact = 0;
            std::size_t typed = 0;
            std::size_t firstExact = 0;
        };

        std::map<std::string, std::string, std::less<>> m_types;
        std::vector<Rule> m_rules;
    };

}

#endif // WOLF_ONSETRULES_H
