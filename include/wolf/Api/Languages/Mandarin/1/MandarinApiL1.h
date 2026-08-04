#ifndef WOLF_API_MANDARINAPIL1_H
#define WOLF_API_MANDARINAPIL1_H

#include <filesystem>
#include <string>
#include <vector>

#include <wolf/Language/LanguageContrib.h>

namespace wolf::Api::Mandarin::L1 {

    inline constexpr char API_NAME[] = "cmn";

    inline constexpr char API_CLASS[] = "ai.svs.MandarinLanguage";

    inline constexpr int API_LEVEL = 1;

    /// What a Mandarin language declares about itself.
    class MandarinSchema : public LanguageSchema {
    public:
        inline MandarinSchema() : LanguageSchema(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// The phonemes this language produces, which a consumer matches against what its own
        /// models accept.
        std::vector<std::string> phonemes;
    };

    /// The resources a Mandarin language is backed by.
    class MandarinConfiguration : public LanguageConfiguration {
    public:
        inline MandarinConfiguration() : LanguageConfiguration(API_NAME, API_CLASS, API_LEVEL) {
        }

        /// Path to the dictionary mapping a syllable to its phonemes.
        std::filesystem::path dict;

        /// Path to an optional second dictionary, whose entries win over the first. This is how a
        /// package adds or corrects a handful of syllables without shipping a copy of the whole
        /// dictionary.
        std::filesystem::path extraDict;

        /// Whether a syllable keeps its tone. Off means \c ma1 and \c ma4 read as the same entry.
        bool useTone = false;
    };

}

#endif // WOLF_API_MANDARINAPIL1_H
