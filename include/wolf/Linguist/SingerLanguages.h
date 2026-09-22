#ifndef WOLF_SINGERLANGUAGES_H
#define WOLF_SINGERLANGUAGES_H

#include <string>
#include <vector>

#include <synthrt/Core/ContribSpec.h>
#include <synthrt/Support/Expected.h>

#include <wolf/wolf_global.h>

namespace wolf {

    /// One entry of a singer's language map.
    struct SingerLanguage {
        /// ISO 639-3 handle the singer uses to name this language.
        std::string language;

        /// Role of the import that provides it, unique within the singer declaration.
        std::string role;
    };

    /// The language map and default language a singer declares.
    struct SingerLanguages {
        std::vector<SingerLanguage> entries;
        std::string defaultLanguage;

        inline bool empty() const noexcept {
            return entries.empty();
        }
    };

    /// Reads the language map of \a singer.
    ///
    /// \c languages and \c defaultLanguage are fields the singer category adds to every singer
    /// declaration and parses before any provider is chosen, the same way it reads the avatar.
    /// synthrt has already checked their shape and that every role names one of the singer's
    /// imports, so a malformed map never loads. This function only presents the parsed map in
    /// wolf's shape, ordered by handle as the JSON object is.
    ///
    /// A singer that declares no languages is not an error and yields an empty result. Whether a
    /// role leads to a linguist contribution, and whether that contribution agrees about the
    /// language, stays with wolf: see SingerLanguageValidator.
    ///
    /// \pre \a singer belongs to the \c singer category. Any other category is refused.
    WOLF_EXPORT srt::Expected<SingerLanguages> readSingerLanguages(const srt::ContribSpec &singer);

}

#endif // WOLF_SINGERLANGUAGES_H
