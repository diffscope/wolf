#ifndef WOLF_LANGUAGEPROVIDER_H
#define WOLF_LANGUAGEPROVIDER_H

#include <synthrt/Support/Expected.h>

#include <wolf/Language/LanguageContrib.h>

namespace wolf {

    /// Implements one kind of language, named by the \c class field of a language manifest.
    class LanguageProvider : public srt::NamedObject {
    public:
        /// The highest language API version this provider supports.
        virtual int apiLevel() const = 0;

        /// Called when \c LanguageSpec loads.
        virtual srt::Expected<srt::UNO<LanguageSchema>>
            createSchema(const LanguageSpec *spec) const = 0;

        /// Called when \c LanguageSpec loads.
        virtual srt::Expected<srt::UNO<LanguageConfiguration>>
            createConfiguration(const LanguageSpec *spec) const = 0;
    };

}

#endif // WOLF_LANGUAGEPROVIDER_H
