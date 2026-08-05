#ifndef WOLF_LANGUAGECONTRIB_H
#define WOLF_LANGUAGECONTRIB_H

#include <synthrt/Core/Contribute.h>
#include <synthrt/Support/DisplayText.h>

#include <wolf/wolf_global.h>

namespace wolf {

    /// Information about a language, produced by whichever provider implements it.
    ///
    /// The provider decides what the manifest's \c schema and \c configuration objects mean, so
    /// what comes back is one of its own types. All this base carries is the part synthrt and wolf
    /// can read without knowing the provider: which one it was, and at what API level.
    class LanguageInfoBase : public srt::NamedObject {
    public:
        inline LanguageInfoBase(std::string name, std::string className, int apiLevel)
            : srt::NamedObject(std::move(name)), _className(std::move(className)),
              _apiLevel(apiLevel) {
        }
        virtual ~LanguageInfoBase() = default;

        inline const std::string &className() const {
            return _className;
        }
        inline int apiLevel() const {
            return _apiLevel;
        }

    protected:
        std::string _className;
        int _apiLevel;
    };

    class LanguageSchema : public LanguageInfoBase {
    public:
        inline LanguageSchema(std::string name, std::string className, int apiLevel)
            : LanguageInfoBase(std::move(name), std::move(className), apiLevel) {
        }
    };

    class LanguageConfiguration : public LanguageInfoBase {
    public:
        inline LanguageConfiguration(std::string name, std::string className, int apiLevel)
            : LanguageInfoBase(std::move(name), std::move(className), apiLevel) {
        }
    };

    class LanguageCategory;

    /// One language contributed by a package.
    ///
    /// The manifest entry is a path to a language description file, which names the provider that
    /// implements the language and hands that provider two objects to interpret.
    class WOLF_EXPORT LanguageSpec : public srt::ContribSpec {
    public:
        ~LanguageSpec();

    public:
        const std::string &className() const;
        srt::DisplayText name() const;
        int apiLevel() const;

        const srt::JsonObject &manifestSchema() const;
        /// \note Borrowed. The specification owns it and outlives every use of it.
        LanguageSchema *schema() const;

        const srt::JsonObject &manifestConfiguration() const;
        /// \note Borrowed, as \c schema() is.
        LanguageConfiguration *configuration() const;

        /// The directory the description file lives in, which relative paths inside it resolve
        /// against.
        const std::filesystem::path &path() const;

    protected:
        class Impl;
        LanguageSpec();

        friend class LanguageCategory;
    };

    /// The \c language contribute category.
    ///
    /// Registered from wolf rather than from synthrt, which works because a program links wolf and
    /// so wolf registers before any \c SynthUnit is built. \sa srt::ContribCategoryRegistry.
    class WOLF_EXPORT LanguageCategory : public srt::ContribCategory {
    public:
        ~LanguageCategory();

    public:
        std::vector<LanguageSpec *> findLanguages(const srt::ContribLocator &locator) const;
        std::vector<LanguageSpec *> languages() const;

    protected:
        srt::Expected<srt::ContribSpec *> parseSpec(const std::filesystem::path &basePath,
                                                    const srt::JsonValue &config) const override;
        srt::Expected<void> loadSpec(srt::ContribSpec *spec,
                                     srt::ContribSpec::State state) override;

    protected:
        class Impl;
        explicit LanguageCategory(srt::SynthUnit *su);

        friend class srt::SynthUnit;
        friend class srt::ContribCategoryFactory<LanguageCategory>;
    };

}

#endif // WOLF_LANGUAGECONTRIB_H
