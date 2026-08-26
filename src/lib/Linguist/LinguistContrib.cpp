#include <wolf/Linguist/LinguistContrib.h>

#include <set>
#include <string_view>

#include <synthrt/Core/ContribImportBinding.h>

#include <wolf/Linguist/LinguistProvider.h>
#include <wolf/Linguist/LinguistProviderPlugin.h>

namespace wolf {

    namespace {

        srt::Expected<void> validateEntry(const srt::JsonObject &entry) {
            static const std::set<std::string_view> fields = {"id", "path"};
            for (const auto &item : entry) {
                if (fields.find(item.first) == fields.end()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      "linguist contribution entry has an unknown field");
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
                                      "linguist declaration has an unknown field");
                }
            }
            return {};
        }

    }

    LinguistSpec::LinguistSpec(const srt::ContribCreateContext &context) : ContribSpec(context) {
    }

    LinguistSpec::~LinguistSpec() = default;

    srt::Expected<std::unique_ptr<srt::ContribExecutiveFactory>>
        LinguistSpec::createExecutiveFactory(srt::ContribImportBinding &binding) const {
        auto value = interpreter();
        if (!value) {
            return srt::Error(srt::Error::FeatureNotSupported,
                              "cannot create a linguist execution factory without a provider");
        }
        return value->as<LinguistProvider>()->createExecutiveFactory(binding);
    }

    LinguistCategory::LinguistCategory()
        : ContribCategory(LINGUIST_CATEGORY, ModuleDeclaration, LinguistProviderPlugin::IID) {
    }

    LinguistCategory::~LinguistCategory() = default;

    std::vector<LinguistSpec *> LinguistCategory::linguists() const {
        std::vector<LinguistSpec *> result;
        const auto values = contributions();
        result.reserve(values.size());
        for (auto value : values) {
            result.push_back(value->as<LinguistSpec>());
        }
        return result;
    }

    srt::Expected<std::unique_ptr<srt::ContribSpec>>
        LinguistCategory::createSpec(const srt::ContribCreateContext &context) const {
        if (auto result = validateEntry(context.manifestEntry()); !result) {
            return result.takeError();
        }
        if (!context.manifestDeclaration() || !context.declarationPath()) {
            return srt::Error(srt::Error::InvalidFormat,
                              "linguist contribution requires a declaration");
        }
        if (auto result = validateDeclaration(*context.manifestDeclaration()); !result) {
            return result.takeError();
        }
        return std::unique_ptr<srt::ContribSpec>(new LinguistSpec(context));
    }

    srt::Expected<std::unique_ptr<srt::ContribExecutiveFactory>>
        LinguistCategory::createExecutiveFactory(srt::ContribImportBinding &binding) const {
        return binding.target().as<LinguistSpec>()->createExecutiveFactory(binding);
    }

}

static srt::ContribCategoryRegistry::Add<wolf::LinguistCategory>
    linguistCategoryRegistration(wolf::LINGUIST_CATEGORY, "");
