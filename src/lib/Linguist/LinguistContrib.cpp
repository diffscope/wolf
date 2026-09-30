#include "LinguistContrib.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <stdcorelib/path.h>

#include <synthrt/Core/ContribImportBinding.h>

#include "Logging.h"
#include "ManifestValues.h"
#include "LinguistProvider.h"
#include "LinguistProviderPlugin.h"

namespace wolf {

    namespace {

        /// Reports a desc.json entry key that this category does not read.
        ///
        /// The report is a warning for the same reason as for the declaration root below. The
        /// JSON profile of the specification requires that an unknown field not invalidate a
        /// document, and this category defines the structure of the entry, so an entry with a
        /// field added by a later wolf version must load in this version.
        void warnAboutUnknownEntryFields(const srt::JsonObject &entry) {
            static const std::set<std::string_view> fields = {"id", "path"};
            for (const auto &item : entry) {
                if (fields.find(item.first) == fields.end()) {
                    logCategory().srtWarning("linguist contribution entry contains the unknown "
                                             "field \"%1\"; the field is ignored",
                                             item.first);
                }
            }
        }

        /// Reports a declaration root key that neither the framework nor this category defines.
        ///
        /// The report is a warning instead of a failure. The upper specification defines the
        /// module declaration root and requires that unknown fields in such objects not cause
        /// rejection. A category that rejected them would cause every field that the framework
        /// adds later to fail to load in earlier builds of wolf. Strict validation applies where
        /// wolf defines the schema: `exports`, `configuration` and `imports[].options`.
        ///
        /// The warning loses no error. A misspelled required field still fails, because the
        /// intended field is then missing. Only a misspelled optional field is reported as a
        /// warning instead of an error, which is the tradeoff that the specification requires.
        void warnAboutUnknownFields(const srt::JsonObject &declaration,
                                    const std::filesystem::path *path) {
            // Common module fields of the framework, as defined by the upper specification. `vars`
            // is included although the loader expands and removes it before a category receives
            // the declaration, so that the list is complete.
            static const std::set<std::string_view> framework = {
                "configuration", "exports", "imports", "interface",
                "level",         "name",    "variant", "vars",
            };
            // Fields that this category adds to the root, parsed before an interpreter is
            // selected.
            static const std::set<std::string_view> category = {"language", "scheme"};

            for (const auto &item : declaration) {
                if (framework.count(item.first) != 0 || category.count(item.first) != 0) {
                    continue;
                }
                logCategory().srtWarning(
                    "linguist declaration %1 contains the unknown field \"%2\"; the field is "
                    "ignored",
                    path ? stdc::path::to_utf8(*path) : std::string("<unknown>"), item.first);
            }
        }

        srt::Expected<std::string> readIdentity(const srt::JsonObject &declaration,
                                                std::string_view field,
                                                bool (*isWellFormed)(std::string_view)) {
            const auto it = declaration.find(field);
            if (it == declaration.end() || !it->second.isString()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "linguist declaration requires a string " + std::string(field));
            }
            auto value = it->second.toString();
            if (!isWellFormed(value)) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "linguist declaration has a malformed " + std::string(field));
            }
            return value;
        }

    }

    LinguistSpec::LinguistSpec(const srt::ContribCreateContext &context, std::string language,
                               std::string scheme)
        : ContribSpec(context), m_language(std::move(language)), m_scheme(std::move(scheme)) {
    }

    LinguistSpec::~LinguistSpec() = default;

    const std::string &LinguistSpec::language() const {
        return m_language;
    }

    const std::string &LinguistSpec::scheme() const {
        return m_scheme;
    }

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
        warnAboutUnknownEntryFields(context.manifestEntry());
        if (!context.manifestDeclaration() || !context.declarationPath()) {
            return srt::Error(srt::Error::InvalidFormat,
                              "linguist contribution requires a declaration");
        }
        const auto &declaration = *context.manifestDeclaration();
        warnAboutUnknownFields(declaration, context.declarationPath());
        auto language = readIdentity(declaration, "language", isLanguageHandle);
        if (!language) {
            return language.takeError();
        }
        auto scheme = readIdentity(declaration, "scheme", isSchemeName);
        if (!scheme) {
            return scheme.takeError();
        }
        // The contribution ID convention <language>-<scheme>[-<qualifier>] is a packaging lint
        // rule, not a load condition. The loader never reads the ID, and enforcing the convention
        // would only remove the naming freedom that the upper specification grants each Package.
        return std::unique_ptr<srt::ContribSpec>(
            new LinguistSpec(context, language.take(), scheme.take()));
    }

    srt::Expected<std::unique_ptr<srt::ContribExecutiveFactory>>
        LinguistCategory::createExecutiveFactory(srt::ContribImportBinding &binding) const {
        return binding.target().as<LinguistSpec>()->createExecutiveFactory(binding);
    }

}

namespace wolf {

    void linkLinguistCategory() noexcept {
    }

}

static srt::ContribCategoryRegistry::Add<wolf::LinguistCategory>
    linguistCategoryRegistration(wolf::LINGUIST_CATEGORY, "");
