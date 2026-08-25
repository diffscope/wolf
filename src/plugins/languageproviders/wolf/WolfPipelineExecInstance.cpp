#include "WolfPipelineExecInstance.h"

#include <algorithm>
#include <utility>

namespace wolf {

    WolfPipelineExecInstance::WolfPipelineExecInstance(srt::SingerSpec &spec,
                                                       std::vector<std::string> languageRoles)
        : Api::Language::L1::WolfPipelineExecInstance(spec),
          m_languageRoles(std::move(languageRoles)) {
    }

    WolfPipelineExecInstance::~WolfPipelineExecInstance() = default;

    const std::vector<std::string> &WolfPipelineExecInstance::languageRoles() const {
        return m_languageRoles;
    }

    srt::Expected<Api::Language::L1::LanguageExecInstance *>
        WolfPipelineExecInstance::createLanguage(
            std::string_view role,
            const Api::Language::L1::LanguageRuntimeOptions &runtimeOptions) {
        if (std::find(m_languageRoles.begin(), m_languageRoles.end(), role) ==
            m_languageRoles.end()) {
            return srt::Error(srt::Error::InvalidArgument,
                              "singer does not import the requested language role");
        }
        auto result = createChild(role, runtimeOptions);
        if (!result) {
            return result.takeError();
        }
        return (*result)->as<Api::Language::L1::LanguageExecInstance>();
    }

}
