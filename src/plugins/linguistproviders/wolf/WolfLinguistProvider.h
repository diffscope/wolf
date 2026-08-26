#ifndef WOLF_WOLFLINGUISTPROVIDER_H
#define WOLF_WOLFLINGUISTPROVIDER_H

#include <memory>
#include <vector>

#include <wolf/Linguist/LinguistProvider.h>

namespace wolf {

    class WolfLinguistProvider : public LinguistProvider {
    public:
        WolfLinguistProvider();
        ~WolfLinguistProvider();

        srt::Expected<std::vector<std::unique_ptr<srt::ContribImportValidator>>>
            createImportValidators() const override;

        srt::Expected<std::vector<std::unique_ptr<srt::ContribSpecExtension>>>
            createExtensions(srt::ContribSpec &spec) const override;

        srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
            createImportOptions(const srt::ContribSpec &target,
                                const srt::JsonValue &manifestOptions) const override;

        srt::Expected<std::unique_ptr<srt::ContribExecutiveFactory>>
            createExecutiveFactory(srt::ContribImportBinding &binding) const override;

        srt::Expected<std::unique_ptr<srt::ContribExports>>
            createExports(const srt::ContribSpec &spec) const override;

        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &spec) const override;
    };

}

#endif // WOLF_WOLFLINGUISTPROVIDER_H
