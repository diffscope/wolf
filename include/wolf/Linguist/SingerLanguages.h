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

        /// Role of the import that provides the language, unique within the singer declaration.
        std::string role;
    };

    /// Language map and default language declared by a singer.
    struct SingerLanguages {
        std::vector<SingerLanguage> entries;
        std::string defaultLanguage;

        inline bool empty() const noexcept {
            return entries.empty();
        }
    };

    /// Reads the language map of \a singer.
    ///
    /// \c languages and \c defaultLanguage are fields that the singer category adds to every
    /// singer declaration and parses before a provider is selected, in the same way as the avatar.
    /// synthrt has already validated their structure and verified that every role names an import
    /// of the singer, and a malformed map therefore fails to load. This function converts the
    /// parsed map to the wolf structure, ordered by handle in the same order as the JSON object.
    ///
    /// A singer without declared languages is not an error and yields an empty result. wolf
    /// verifies whether a role resolves to a linguist contribution and whether that contribution
    /// declares the same language. The import validator of the wolf linguist provider
    /// (src/plugins/linguistproviders/wolf) checks both conditions before Commit.
    ///
    /// \pre \a singer belongs to the \c singer category. Any other category is rejected.
    WOLF_EXPORT srt::Expected<SingerLanguages> readSingerLanguages(const srt::ContribSpec &singer);

}

#endif // WOLF_SINGERLANGUAGES_H
