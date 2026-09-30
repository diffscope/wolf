#ifndef WOLF_VERIFIER_H
#define WOLF_VERIFIER_H

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <synthrt/Support/Expected.h>
#include <synthrt/Support/JSON.h>


namespace wolf {

    /// Treatment of a word by the steps that follow the classification.
    enum class VerifyMode {
        Convert,
        Copy,
    };

    /// One classification rule.
    ///
    /// The G2P orchestration variant and the pinyin engine variant classify words identically and
    /// therefore share this structure instead of defining separate variants of it.
    struct VerifyEntry {
        /// One of regex, array or dict. All three must be supported. An unknown type is a load
        /// failure instead of a silently dropped rule.
        std::string type;

        /// Regular expressions, literal words, or dictionary paths, depending on \a type.
        std::vector<std::string> value;

        VerifyMode mode = VerifyMode::Convert;
    };

    /// Classifies words as convert or copy.
    ///
    /// Entries are applied in declaration order, and a later match overrides an earlier one, so
    /// that a declaration forms a list of increasingly specific overrides. A word that no entry
    /// matches is classified as copy.
    class Verifier {
    public:
        /// Reads the entries and compiles or loads the resources that each entry requires.
        ///
        /// A relative dict path is resolved against \a base, the directory of the declaration
        /// that contains the entries.
        static srt::Expected<Verifier> create(const std::vector<VerifyEntry> &entries,
                                              const std::filesystem::path &base);

        Verifier(Verifier &&other) noexcept;
        Verifier &operator=(Verifier &&other) noexcept;
        ~Verifier();

        /// Returns one mode per word, in the same order.
        std::vector<VerifyMode> classify(const std::vector<std::string> &words) const;

    private:
        Verifier();

        class Impl;
        std::unique_ptr<Impl> _impl;
    };

    /// Reads an array of verify entries.
    ///
    /// \a what names the enclosing key in error messages. Entry structures are validated here, so
    /// that a malformed rule fails the package instead of the first word that the rule would
    /// match.
    srt::Expected<std::vector<VerifyEntry>> readVerifyEntries(const srt::JsonValue &value,
                                                              std::string_view what);

}

#endif // WOLF_VERIFIER_H
