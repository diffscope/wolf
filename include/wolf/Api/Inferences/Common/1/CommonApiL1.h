#ifndef WOLF_API_COMMONAPIL1_H
#define WOLF_API_COMMONAPIL1_H

#include <string>
#include <utility>

namespace wolf::Api::Common::L1 {

    /// A language handle paired with the phonetic notation its pronunciation layer uses.
    ///
    /// This pair is the only matching key of the linguist contract family. Contribution IDs are
    /// never used for matching.
    ///
    /// The notation is scoped by the language. The same scheme name under two languages does not
    /// denote interchangeable notations, and comparisons therefore always cover both fields.
    struct LanguageScheme {
        LanguageScheme() = default;

        LanguageScheme(std::string language, std::string scheme)
            : language(std::move(language)), scheme(std::move(scheme)) {
        }

        /// ISO 639-3 code.
        std::string language;

        /// Notation name, unique within the language.
        std::string scheme;

        inline bool empty() const noexcept {
            return language.empty() && scheme.empty();
        }
    };

    inline bool operator==(const LanguageScheme &lhs, const LanguageScheme &rhs) noexcept {
        return lhs.language == rhs.language && lhs.scheme == rhs.scheme;
    }

    inline bool operator!=(const LanguageScheme &lhs, const LanguageScheme &rhs) noexcept {
        return !(lhs == rhs);
    }

}

#endif // WOLF_API_COMMONAPIL1_H
