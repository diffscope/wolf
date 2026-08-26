#include "WolfPipelineExecutive.h"

#include <algorithm>
#include <utility>

namespace wolf {

    WolfPipelineExecutive::WolfPipelineExecutive(srt::SingerSpec &spec,
                                                 std::vector<std::string> linguistRoles)
        : Api::Linguist::L1::WolfPipelineExecutive(spec),
          m_linguistRoles(std::move(linguistRoles)) {
    }

    WolfPipelineExecutive::~WolfPipelineExecutive() = default;

    const std::vector<std::string> &WolfPipelineExecutive::linguistRoles() const {
        return m_linguistRoles;
    }

    srt::Expected<Api::Linguist::L1::LinguistExecutive *> WolfPipelineExecutive::createLinguist(
        std::string_view role, const Api::Linguist::L1::LinguistRuntimeOptions &runtimeOptions) {
        if (std::find(m_linguistRoles.begin(), m_linguistRoles.end(), role) ==
            m_linguistRoles.end()) {
            return srt::Error(srt::Error::InvalidArgument,
                              "singer does not import the requested linguist role");
        }
        auto result = createChild(role, runtimeOptions);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Linguist::L1::LinguistExecutive>();
    }

}
