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

        /// Reports a desc.json entry key this category does not read.
        ///
        /// A warning, for the same reason as the declaration root below: the specification's JSON
        /// profile says an unknown field must not make a document invalid, and the entry's shape
        /// is this category's to define, so a field a later wolf adds must load against this one.
        void warnAboutUnknownEntryFields(const srt::JsonObject &entry) {
            static const std::set<std::string_view> fields = {"id", "path"};
            for (const auto &item : entry) {
                if (fields.find(item.first) == fields.end()) {
                    logCategory().srtWarning("linguist contribution entry carries the unknown "
                                             "field \"%1\"; it is ignored",
                                             item.first);
                }
            }
        }

        /// Reports a declaration root key that is neither the framework's nor this category's.
        ///
        /// A warning rather than a failure. The module declaration root is an object the upper
        /// specification defines, and it says of such objects that unknown fields must not cause
        /// rejection — a category that rejects them makes every field the framework adds later
        /// fail to load against builds of wolf that predate it. The strict treatment stays where
        /// wolf really does own the schema: `exports`, `configuration` and `imports[].options`.
        ///
        /// Nothing is lost by warning here. A misspelt required field still fails, because the
        /// field it should have been is then missing; only a misspelt optional one degrades from
        /// an error to a warning, which is the trade the specification asks for.
        void warnAboutUnknownFields(const srt::JsonObject &declaration,
                                    const std::filesystem::path *path) {
            // The framework's common module fields, per the upper specification. `vars` is in the
            // set although the loader expands and removes it before a category sees the
            // declaration: leaving it out would make this list read as if it were the whole story.
            static const std::set<std::string_view> framework = {
                "configuration", "exports", "imports", "interface",
                "level",         "name",    "variant", "vars",
            };
            // The fields this category adds to the root, parsed before an interpreter is chosen.
            static const std::set<std::string_view> category = {"language", "scheme"};

            for (const auto &item : declaration) {
                if (framework.count(item.first) != 0 || category.count(item.first) != 0) {
                    continue;
                }
                logCategory().srtWarning(
                    "linguist declaration %1 carries the unknown field \"%2\"; it is ignored",
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
        // The contribution ID convention <language>-<scheme>[-<qualifier>] is a packaging lint, not
        // a load condition: nothing here ever reads the ID, so enforcing it would only cost the
        // naming freedom the upper specification grants each Package.
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
