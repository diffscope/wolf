#ifndef WOLF_LANGUAGECONTRIB_H
#define WOLF_LANGUAGECONTRIB_H

#include <memory>
#include <vector>

#include <synthrt/Core/ContribCategory.h>
#include <synthrt/Core/ContribSpec.h>

#include <wolf/wolf_global.h>

namespace wolf {

    inline constexpr char LANGUAGE_CATEGORY[] = "org.openvpi.language";

    /// The immutable declaration of one language contribution.
    class WOLF_EXPORT LanguageSpec : public srt::ContribSpec {
    public:
        ~LanguageSpec();

    private:
        explicit LanguageSpec(const srt::ContribCreateContext &context);

        srt::Expected<std::unique_ptr<srt::ContribExecutiveFactory>>
            createExecutiveFactory(srt::ContribImportBinding &binding) const;

        friend class LanguageCategory;
    };

    /// Parses and indexes contributions in the wolf language category.
    class WOLF_EXPORT LanguageCategory : public srt::ContribCategory {
    public:
        LanguageCategory();
        ~LanguageCategory();

        /// Returns all committed language contributions.
        std::vector<LanguageSpec *> languages() const;

    protected:
        srt::Expected<std::unique_ptr<srt::ContribSpec>>
            createSpec(const srt::ContribCreateContext &context) const override;

        srt::Expected<std::unique_ptr<srt::ContribExecutiveFactory>>
            createExecutiveFactory(srt::ContribImportBinding &binding) const override;
    };

}

#endif // WOLF_LANGUAGECONTRIB_H
