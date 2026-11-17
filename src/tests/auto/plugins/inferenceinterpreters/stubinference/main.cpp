#include <memory>
#include <string>

#include <stdcorelib/support/json.h>

#include <synthrt/SVS/InferenceInterpreterPlugin.h>

#include <wolf/Api/Inferences/G2p/1/G2pApiL1.h>
#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>
#include <wolf/Api/Inferences/S2p/1/S2pApiL1.h>

#include "StubExecutives.h"

// The stub interpreters are a static plugin of the test executables that link these objects. A
// static plugin precedes the plugins of the search paths, so the stub declares only the variants
// that no plugin of this build serves, and never competes with a real interpreter for a triplet.

namespace wolf::stub {

    namespace {

        class StubInferencePlugin final : public srt::InferenceInterpreterPlugin {
        public:
            srt::Expected<std::unique_ptr<srt::ContribInterpreter>>
                create(const srt::ContribContractTriplet &triplet) override {
                if (triplet.interfaceId() == Api::G2p::L1::Contract.interfaceId() &&
                    triplet.level() == Api::G2p::L1::Contract.level()) {
                    return std::unique_ptr<srt::ContribInterpreter>(
                        new StubG2pInterpreter(triplet.variant()));
                }
                if (triplet.interfaceId() == Api::S2p::L1::Contract.interfaceId() &&
                    triplet.level() == Api::S2p::L1::Contract.level()) {
                    return std::unique_ptr<srt::ContribInterpreter>(
                        new StubS2pInterpreter(triplet.variant()));
                }
                if (triplet.interfaceId() == Api::Onset::L1::Contract.interfaceId() &&
                    triplet.level() == Api::Onset::L1::Contract.level()) {
                    return std::unique_ptr<srt::ContribInterpreter>(
                        new StubOnsetInterpreter(triplet.variant()));
                }
                return srt::Error(srt::Error::InvalidArgument,
                                  "unsupported inference interpreter contract");
            }
        };

        stdc::json::Object g2pDeclaration(const char *variant) {
            return stdc::json::Object{
                {"interface", Api::G2p::L1::Contract.interfaceId()},
                {"level",     Api::G2p::L1::Contract.level()      },
                {"variant",   variant                             },
            };
        }

        // stub-miscount is a variant that no real interpreter declares. It lets a fixture request
        // a module that violates the provider ABI by returning a shorter batch than its input.
        // multig2p-onnx is served by the stub only in a build without the real multig2p.
        stdc::json::Value inferenceMetadata() {
            stdc::json::Array interpreters{g2pDeclaration("stub-miscount")};
#ifndef WOLF_TEST_HAS_MULTIG2P
            interpreters.push_back(g2pDeclaration("multig2p-onnx"));
#endif
            return stdc::json::Object{
                {"name",         "wolfstubinference"},
                {"interpreters", interpreters       },
            };
        }

        STDC_EXPORT_STATIC_PLUGIN(StubInferencePlugin, srt::InferenceInterpreterPlugin::IID,
                                  inferenceMetadata())

    }

}
