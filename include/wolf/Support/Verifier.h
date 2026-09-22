#ifndef WOLF_VERIFIER_H
#define WOLF_VERIFIER_H

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <synthrt/Support/Expected.h>
#include <synthrt/Support/JSON.h>

#include <wolf/wolf_global.h>

namespace wolf {

    /// How a word is to be treated by the steps that follow the classification.
    enum class VerifyMode {
        Convert,
        Copy,
    };

    /// One classification rule.
    ///
    /// The G2P orchestration variant and the pinyin engine variant classify words identically, so
    /// they share this shape rather than each spelling out a dialect of it.
    struct VerifyEntry {
        /// One of regex, array or dict. All three must be honoured; an unknown type is a load
        /// failure rather than a silently dropped rule.
        std::string type;

        /// Regular expressions, literal words, or dictionary paths, depending on \a type.
        std::vector<std::string> value;

        VerifyMode mode = VerifyMode::Convert;
    };

    /// Classifies words as convert or copy.
    ///
    /// Entries are applied in declaration order and a later match overrides an earlier one, so a
    /// declaration reads as a list of increasingly specific overrides. A word matched by nothing is
    /// copy.
    class WOLF_EXPORT Verifier {
    public:
        /// Reads the entries and compiles or loads whatever each one needs.
        ///
        /// A relative dict path resolves against \a base, the directory of the declaration the
        /// entries came from.
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
    /// \a what names the enclosing key in error messages. Entry shapes are validated here so that
    /// a malformed rule fails the package rather than the first word that would have matched it.
    WOLF_EXPORT srt::Expected<std::vector<VerifyEntry>>
        readVerifyEntries(const srt::JsonValue &value, std::string_view what);

}

#endif // WOLF_VERIFIER_H
