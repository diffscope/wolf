#include <wolf/Language/LanguageContrib.h>

#include <set>
#include <string_view>

#include <wolf/Language/LanguageInterpreterPlugin.h>

namespace wolf {

    srt::Expected<std::unique_ptr<srt::ContribExecFactory>>
        createLanguageExecFactory(srt::ContribImportBinding &binding);

    namespace {

        srt::Expected<void> validateEntry(const srt::JsonObject &entry) {
            static const std::set<std::string_view> fields = {"id", "path"};
            for (const auto &item : entry) {
                if (fields.find(item.first) == fields.end()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "language contribution entry has an unknown field");
                }
            }
            return {};
        }

        srt::Expected<void> validateDeclaration(const srt::JsonObject &declaration) {
            static const std::set<std::string_view> fields = {
                "configuration", "exports", "imports", "interface", "level", "name", "variant",
            };
            for (const auto &item : declaration) {
                if (fields.find(item.first) == fields.end()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "language declaration has an unknown field");
                }
            }
            return {};
        }

    }

    LanguageSpec::LanguageSpec(const srt::ContribCreateContext &context) : ContribSpec(context) {
    }

    LanguageSpec::~LanguageSpec() = default;

    LanguageCategory::LanguageCategory()
        : ContribCategory(LANGUAGE_CATEGORY, ModuleDeclaration, LanguageInterpreterPlugin::IID) {
    }

    LanguageCategory::~LanguageCategory() = default;

    std::vector<LanguageSpec *> LanguageCategory::languages() const {
        std::vector<LanguageSpec *> result;
        const auto values = contributions();
        result.reserve(values.size());
        for (auto *value : values) {
            result.push_back(value->as<LanguageSpec>());
        }
        return result;
    }

    srt::Expected<std::unique_ptr<srt::ContribSpec>>
        LanguageCategory::createSpec(const srt::ContribCreateContext &context) const {
        if (auto result = validateEntry(context.manifestEntry()); !result) {
            return result.takeError();
        }
        if (!context.manifestDeclaration() || !context.declarationPath()) {
            return srt::Error(srt::Error::InvalidFormat,
                              "language contribution requires a declaration");
        }
        if (auto result = validateDeclaration(*context.manifestDeclaration()); !result) {
            return result.takeError();
        }
        return std::unique_ptr<srt::ContribSpec>(new LanguageSpec(context));
    }

    srt::Expected<std::unique_ptr<srt::ContribExecFactory>>
        LanguageCategory::createExecFactory(srt::ContribImportBinding &binding) const {
        return createLanguageExecFactory(binding);
    }

}

static srt::ContribCategoryRegistry::Add<wolf::LanguageCategory>
    languageCategoryRegistration(wolf::LANGUAGE_CATEGORY, "");
