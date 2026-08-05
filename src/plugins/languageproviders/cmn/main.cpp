#include <wolf/Api/Languages/Mandarin/1/MandarinApiL1.h>
#include <wolf/Language/LanguageProviderPlugin.h>

#include "MandarinProvider.h"

namespace wolf {

    namespace Cmn = Api::Mandarin::L1;

    class MandarinProviderPlugin : public LanguageProviderPlugin {
    public:
        MandarinProviderPlugin() = default;

        const char *key() const override {
            return Cmn::API_CLASS;
        }

        srt::UNO<LanguageProvider> create() override {
            return srt::UNO<MandarinProvider>::create();
        }
    };

}

SYNTHRT_EXPORT_PLUGIN(wolf::MandarinProviderPlugin)
