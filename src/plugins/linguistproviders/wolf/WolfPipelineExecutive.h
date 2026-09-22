#ifndef WOLF_WOLFPIPELINEEXECUTIVE_H
#define WOLF_WOLFPIPELINEEXECUTIVE_H

#include <string>
#include <string_view>
#include <vector>

#include <synthrt/Core/ContribLocator.h>

#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>
#include <wolf/Linguist/SingerLanguages.h>

namespace wolf {

    /// Aggregates the linguist contributions one singer declares.
    ///
    /// It is keyed by language handle throughout. The import role behind a handle is an
    /// implementation detail of the singer declaration, and callers think in languages.
    class WolfPipelineExecutive : public Api::Linguist::L1::WolfPipelineExecutive {
    public:
        WolfPipelineExecutive(srt::SingerSpec &spec, SingerLanguages languages);
        ~WolfPipelineExecutive();

        const std::vector<std::string> &languages() const override;

        srt::Expected<Api::Linguist::L1::LinguistExecutive *>
            createLinguist(std::string_view language,
                           const Api::Linguist::L1::LinguistRuntimeOptions &runtimeOptions) override;

    private:
        SingerLanguages m_languages;
        std::vector<std::string> m_handles;
    };

}

#endif // WOLF_WOLFPIPELINEEXECUTIVE_H
