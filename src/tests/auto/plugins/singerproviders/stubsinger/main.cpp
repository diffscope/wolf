#include <memory>
#include <string>

#include <stdcorelib/support/json.h>

#include <synthrt/SVS/SingerProviderPlugin.h>

#include "StubSingerProvider.h"

// The stub singer provider is a static plugin of the test executables that link these objects.

namespace wolf::stub {

    namespace {

        class StubSingerPlugin final : public srt::SingerProviderPlugin {
        public:
            srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
                create(const srt::ContribContractTriplet &triplet) override {
                if (triplet != srt::ContribContractTriplet(std::string(INTERFACE), LEVEL,
                                                           std::string(VARIANT))) {
                    return srt::Error(srt::Error::InvalidArgument,
                                      "unsupported stub singer contract: " + triplet.toString());
                }
                return std::unique_ptr<srt::ContribInterpreter>(new StubSingerProvider(triplet));
            }
        };

        stdc::json::Value singerMetadata() {
            return stdc::json::Object{
                {"name",         "wolfstubsinger"                  },
                {"interpreters", stdc::json::Array{stdc::json::Object{
                                     {"interface", std::string(INTERFACE)},
                                     {"level", LEVEL},
                                     {"variant", std::string(VARIANT)},
                                 }}},
            };
        }

        STDC_EXPORT_STATIC_PLUGIN(StubSingerPlugin, srt::SingerProviderPlugin::IID,
                                  singerMetadata())

    }

}
