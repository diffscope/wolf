#include <wolf/Api/Languages/Language/1/LanguageApiL1.h>
#include <wolf/Language/LanguageInterpreterPlugin.h>

#include "WolfLanguageInterpreter.h"

namespace wolf {

    class WolfLanguageInterpreterPlugin : public LanguageInterpreterPlugin {
    public:
        WolfLanguageInterpreterPlugin() = default;

        srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
            create(std::string_view interfaceName, int level, std::string_view variant) override {
            namespace Lang = Api::Language::L1;
            if (interfaceName != Lang::API_INTERFACE || level != Lang::API_LEVEL ||
                variant != Lang::API_VARIANT) {
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported language interpreter contract");
            }
            return std::unique_ptr<srt::ContribInterpreter>(new WolfLanguageInterpreter());
        }
    };

}

STDC_EXPORT_PLUGIN(wolf::WolfLanguageInterpreterPlugin)
