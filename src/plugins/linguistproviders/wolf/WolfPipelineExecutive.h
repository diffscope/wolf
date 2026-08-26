#ifndef WOLF_WOLFPIPELINEEXECUTIVE_H
#define WOLF_WOLFPIPELINEEXECUTIVE_H

#include <string>
#include <string_view>
#include <vector>

#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>

namespace wolf {

    class WolfPipelineExecutive : public Api::Linguist::L1::WolfPipelineExecutive {
    public:
        WolfPipelineExecutive(srt::SingerSpec &spec, std::vector<std::string> linguistRoles);
        ~WolfPipelineExecutive();

        const std::vector<std::string> &linguistRoles() const override;

        srt::Expected<Api::Linguist::L1::LinguistExecutive *> createLinguist(
            std::string_view role,
            const Api::Linguist::L1::LinguistRuntimeOptions &runtimeOptions) override;

    private:
        std::vector<std::string> m_linguistRoles;
    };

}

#endif // WOLF_WOLFPIPELINEEXECUTIVE_H
