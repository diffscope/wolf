#ifndef WOLF_WOLFLANGUAGEPROVIDER_H
#define WOLF_WOLFLANGUAGEPROVIDER_H

#include <memory>
#include <vector>

#include <wolf/Language/LanguageProvider.h>

namespace wolf {

    class WolfLanguageProvider : public LanguageProvider {
    public:
        WolfLanguageProvider();
        ~WolfLanguageProvider();

        srt::Expected<std::vector<std::unique_ptr<srt::ContribImportValidator>>>
            createImportValidators() const override;

        srt::Expected<std::vector<std::unique_ptr<srt::ContribSpecExtension>>>
            createExtensions(srt::ContribSpec &spec) const override;

        srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
            createImportOptions(const srt::ContribSpec &target,
                                const srt::JsonValue &manifestOptions) const override;

        srt::Expected<std::unique_ptr<srt::ContribExecFactory>>
            createExecFactory(srt::ContribImportBinding &binding) const override;

        srt::Expected<std::unique_ptr<srt::ContribExports>>
            createExports(const srt::ContribSpec &spec) const override;

        srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
            createConfiguration(const srt::ContribSpec &spec) const override;
    };

}

#endif // WOLF_WOLFLANGUAGEPROVIDER_H
