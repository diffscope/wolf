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
    /// wildcard `*`. Matching runs left to right. At each position the best-fitting rule is applied
    /// and the scan advances past its pattern. A position that no rule covers is marked false,
    /// which the contract defines as "not an onset" rather than as an error.
    ///
    /// The best-fitting rule has the longest pattern. Ties are broken by the number of literal
    /// segments, then by the number of typed segments, then by the earliest literal segment.
    /// Ranking literal segments above typed segments lets a rule for a specific phoneme override
    /// the rule for its phoneme type.
    class RuleTable {
    public:
        /// Rule file format version supported by this build.
        ///
        /// The formatVersion field is optional. A file without it is read as version 1, which is
        /// the format of every rule file shipped so far. A file that specifies a higher version is
        /// rejected.
        ///
        /// A format change therefore requires raising this constant and the cache generation
        /// `RULE_KIND` together: the version causes older builds to reject the file, and the
        /// generation keeps parses of the previous format out of the cache.
        static constexpr int FORMAT_VERSION = 1;

        static srt::Expected<RuleTable> load(const std::filesystem::path &path);

        std::vector<bool> mark(const std::vector<std::string> &phonemes) const;

    private:
        struct Rule {
            std::vector<std::string> pattern;
            std::vector<std::size_t> onsets;
        };

        /// Match quality of one rule at a position. A rule that does not match has no Fit.
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
