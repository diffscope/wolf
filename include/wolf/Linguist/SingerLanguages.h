#ifndef WOLF_LINGUIST_SINGERLANGUAGES_H
#define WOLF_LINGUIST_SINGERLANGUAGES_H

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
        std::vector<std::string> reservedPhonemes;

        inline bool empty() const noexcept {
            return entries.empty();
        }
    };

    /// Reads the language map of \a singer.
    ///
    /// The fields are part of the DiffSinger singer \c exports. After Load the DiffSinger provider
    /// has interpreted them, and this function reads the typed object. A singer opened with
    /// \c DataOnly has no typed exports, so the function reads the expanded manifest directly.
    ///
    /// A singer without declared languages is not an error and yields an empty result. wolf
    /// verifies whether a role resolves to a linguist contribution and whether that contribution
    /// declares the same language. The import validator of the wolf linguist provider
    /// (src/plugins/linguistproviders/wolflinguist) checks both conditions before Commit.
    ///
    /// \pre \a singer belongs to the \c singer category. Any other category is rejected.
    WOLF_EXPORT srt::Expected<SingerLanguages> readSingerLanguages(const srt::ContribSpec &singer);

}

#endif // WOLF_LINGUIST_SINGERLANGUAGES_H
