#ifndef WOLF_API_COMMONAPIL1_H
#define WOLF_API_COMMONAPIL1_H

#include <string>
#include <utility>

namespace wolf::Api::Common::L1 {

    /// A language handle paired with the phonetic notation its pronunciation layer uses.
    ///
    /// This pair is the only key the linguist contract family matches on. Contribution IDs are
    /// never read for that purpose.
    ///
    /// The notation is scoped by the language: the same scheme name under two languages carries no
    /// interchange promise between them, so comparisons always cover both fields.
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
