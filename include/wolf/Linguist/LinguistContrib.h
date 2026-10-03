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

    /// Ensures that the linguist category is registered with synthrt.
    ///
    /// Registration is performed by a static initializer of this library, which runs whenever the
    /// library is loaded, and this function has an empty body. Its purpose is to be referenced. A
    /// host that links wolf only for the category and references no other symbol would otherwise
    /// lose the dependency to a linker that discards unreferenced libraries, as ELF linkers do
    /// under --as-needed and the MSVC linker does for every import library. A host calls it once
    /// before the first SynthUnit is constructed.
    WOLF_EXPORT void linkLinguistCategory() noexcept;

    /// The immutable declaration of one linguist contribution.
    class WOLF_EXPORT LinguistSpec : public srt::ContribSpec {
    public:
        ~LinguistSpec();

        /// Returns the ISO 639-3 handle of the language this contribution describes.
        ///
        /// Together with scheme() this is the only matching key of the contract family. Both
        /// values are parsed with the manifest and are therefore available in \c DataOnly mode.
        const std::string &language() const;

        /// Returns the phonetic notation of the pronunciation layer of this contribution.
        ///
        /// The notation is scoped by language(). The same name under two languages does not
        /// denote interchangeable notations.
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
