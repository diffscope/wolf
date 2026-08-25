#ifndef WOLF_WOLFPIPELINEEXECUTIVE_H
#define WOLF_WOLFPIPELINEEXECUTIVE_H

#include <string>
#include <string_view>
#include <vector>

#include <wolf/Api/Languages/Language/1/LanguageApiL1.h>

namespace wolf {

    class WolfPipelineExecutive : public Api::Language::L1::WolfPipelineExecutive {
    public:
        WolfPipelineExecutive(srt::SingerSpec &spec, std::vector<std::string> languageRoles);
        ~WolfPipelineExecutive();

        const std::vector<std::string> &languageRoles() const override;

        srt::Expected<Api::Language::L1::LanguageExecutive *> createLanguage(
            std::string_view role,
            const Api::Language::L1::LanguageRuntimeOptions &runtimeOptions) override;

    private:
        std::vector<std::string> m_languageRoles;
    };

}

#endif // WOLF_WOLFPIPELINEEXECUTIVE_H
