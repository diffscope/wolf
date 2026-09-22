#ifndef WOLF_LINGUISTCONTRIB_H
#define WOLF_LINGUISTCONTRIB_H

#include <memory>
#include <string>
#include <vector>

#include <synthrt/Core/ContribCategory.h>
#include <synthrt/Core/ContribSpec.h>

#include <wolf/wolf_global.h>

namespace wolf {

    inline constexpr char LINGUIST_CATEGORY[] = "linguist";

    /// Makes sure the linguist category is registered with synthrt.
    ///
    /// Registration is a static initializer in this library, so it runs whenever the library is
    /// loaded, and this function does nothing. It exists to be named: a host that links wolf only
    /// for the category, without referencing any other symbol, would otherwise have the
    /// dependency dropped by a linker that discards unreferenced libraries, which ELF linkers do
    /// under --as-needed and the MSVC linker does for every import library. Call it once, anywhere
    /// before the first SynthUnit is constructed.
    WOLF_EXPORT void linkLinguistCategory() noexcept;

    /// The immutable declaration of one linguist contribution.
    class WOLF_EXPORT LinguistSpec : public srt::ContribSpec {
    public:
        ~LinguistSpec();

        /// Returns the ISO 639-3 handle of the language this contribution describes.
        ///
        /// Together with scheme() this is the only key the contract family matches on. Both are
        /// parsed while the manifest is read, so they are available in \c DataOnly mode.
        const std::string &language() const;

        /// Returns the phonetic notation its pronunciation layer uses.
        ///
        /// The notation is scoped by language(): the same name under two languages carries no
        /// interchange promise between them.
        const std::string &scheme() const;

    private:
        LinguistSpec(const srt::ContribCreateContext &context, std::string language,
                     std::string scheme);

        srt::Expected<std::unique_ptr<srt::ContribExecutiveFactory>>
            createExecutiveFactory(srt::ContribImportBinding &binding) const;

        std::string m_language;
        std::string m_scheme;

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
