#include "SingerLanguages.h"

#include <synthrt/SVS/SingerContrib.h>

namespace wolf {

    srt::Expected<SingerLanguages> readSingerLanguages(const srt::ContribSpec &singer) {
        if (singer.locator().category() != srt::SingerCategory::NAME) {
            return srt::Error(srt::Error::InvalidArgument,
                              "the language map belongs to singer declarations, not to " +
                                  singer.locator().category());
        }
        // The singer category has already validated the structure of both fields and verified
        // that every role exists among the imports. A package with a malformed map therefore
        // never reaches this point.
        const auto *spec = singer.as<srt::SingerSpec>();
        SingerLanguages result;
        result.entries.reserve(spec->languages().size());
        for (const auto &[language, role] : spec->languages()) {
            result.entries.push_back(SingerLanguage{language, role});
        }
        result.defaultLanguage = spec->defaultLanguage();
        return result;
    }

}
