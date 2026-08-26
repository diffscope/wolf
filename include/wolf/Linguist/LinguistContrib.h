#ifndef WOLF_LINGUISTCONTRIB_H
#define WOLF_LINGUISTCONTRIB_H

#include <memory>
#include <vector>

#include <synthrt/Core/ContribCategory.h>
#include <synthrt/Core/ContribSpec.h>

#include <wolf/wolf_global.h>

namespace wolf {

    inline constexpr char LINGUIST_CATEGORY[] = "linguist";

    /// The immutable declaration of one linguist contribution.
    class WOLF_EXPORT LinguistSpec : public srt::ContribSpec {
    public:
        ~LinguistSpec();

    private:
        explicit LinguistSpec(const srt::ContribCreateContext &context);

        srt::Expected<std::unique_ptr<srt::ContribExecutiveFactory>>
            createExecutiveFactory(srt::ContribImportBinding &binding) const;

        friend class LinguistCategory;
    };

    /// Parses and indexes contributions in the wolf linguist category.
    class WOLF_EXPORT LinguistCategory : public srt::ContribCategory {
    public:
        LinguistCategory();
        ~LinguistCategory();

        /// Returns all committed linguist contributions.
        std::vector<LinguistSpec *> linguists() const;

    protected:
        srt::Expected<std::unique_ptr<srt::ContribSpec>>
            createSpec(const srt::ContribCreateContext &context) const override;

        srt::Expected<std::unique_ptr<srt::ContribExecutiveFactory>>
            createExecutiveFactory(srt::ContribImportBinding &binding) const override;
    };

}

#endif // WOLF_LINGUISTCONTRIB_H
