#ifndef WOLF_LINGUISTPROVIDERS_WOLFLINGUIST_WOLFLINGUISTPROVIDER_H
#define WOLF_LINGUISTPROVIDERS_WOLFLINGUIST_WOLFLINGUISTPROVIDER_H

#include <memory>
#include <vector>

#include <synthrt/Core/ContribContract.h>
#include <synthrt/Core/ContribLocator.h>

#include <wolf/Linguist/LinguistProvider.h>

namespace wolf {

    namespace detail {

        /// Returns whether \a triplet is an inference contract that a linguist role can bind.
        ///
        /// The role types consumed by a linguist are Level 1 inference executives. Matching only
        /// \c interfaceId would accept a later level and then convert the unrelated executive to
        /// the Level 1 type.
        bool isCompatibleRoleTarget(const srt::ContribLocator &locator,
                                    const srt::ContribContractTriplet &triplet,
                                    const srt::ContribContract &expected);

    }

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

#endif // WOLF_LINGUISTPROVIDERS_WOLFLINGUIST_WOLFLINGUISTPROVIDER_H
