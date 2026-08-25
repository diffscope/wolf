#ifndef WOLF_WOLFPIPELINEEXECINSTANCE_H
#define WOLF_WOLFPIPELINEEXECINSTANCE_H

#include <string>
#include <string_view>
#include <vector>

#include <wolf/Api/Languages/Language/1/LanguageApiL1.h>

namespace wolf {

    class WolfPipelineExecInstance : public Api::Language::L1::WolfPipelineExecInstance {
    public:
        WolfPipelineExecInstance(srt::SingerSpec &spec, std::vector<std::string> languageRoles);
        ~WolfPipelineExecInstance();

        const std::vector<std::string> &languageRoles() const override;

        srt::Expected<Api::Language::L1::LanguageExecInstance *> createLanguage(
            std::string_view role,
            const Api::Language::L1::LanguageRuntimeOptions &runtimeOptions) override;

    private:
        std::vector<std::string> m_languageRoles;
    };

}

#endif // WOLF_WOLFPIPELINEEXECINSTANCE_H
