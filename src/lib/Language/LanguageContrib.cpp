#include <wolf/Language/LanguageContrib.h>

#include <fstream>
#include <set>
#include <sstream>

#include <stdcorelib/pimpl.h>
#include <stdcorelib/str.h>
#include <stdcorelib/path.h>

#include <synthrt/Core/Contribute_p.h>
#include <synthrt/Core/SynthUnit.h>

#include <wolf/Language/LanguageProvider.h>
#include <wolf/Language/LanguageProviderPlugin.h>

namespace fs = std::filesystem;

using srt::ContribCategory;
using srt::ContribLocator;
using srt::ContribSpec;
using srt::DisplayText;
using srt::Error;
using srt::Expected;
using srt::JsonObject;
using srt::JsonValue;
using srt::NO;

namespace wolf {

    class LanguageSpec::Impl : public ContribSpec::Impl {
    public:
        Impl() : ContribSpec::Impl("language") {
        }

        Expected<void> read(const fs::path &basePath, const JsonObject &obj) override;
        Expected<void> readDesc(const fs::path &basePath, const JsonValue &pathValue);

        fs::path path;

        std::string className;

        DisplayText name;
        int apiLevel = 0;

        JsonObject manifestSchema;
        srt::UNO<LanguageSchema> schema;

        JsonObject manifestConfiguration;
        srt::UNO<LanguageConfiguration> configuration;

        LanguageProvider *provider = nullptr;
    };

    static Expected<JsonObject> readJsonObjectFile(const fs::path &path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return Error{
                Error::FileNotOpen,
                stdc::formatN(R"(%1: failed to open language manifest)", path),
            };
        }

        std::stringstream ss;
        ss << file.rdbuf();

        std::string error;
        auto root = JsonValue::fromJson(ss.str(), true, &error);
        if (!error.empty()) {
            return Error{
                Error::InvalidFormat,
                stdc::formatN(R"(%1: invalid language manifest format: %2)", path, error),
            };
        }
        if (!root.isObject()) {
            return Error{
                Error::InvalidFormat,
                stdc::formatN(R"(%1: invalid language manifest format)", path),
            };
        }
        return root.toObject();
    }

    Expected<void> LanguageSpec::Impl::readDesc(const fs::path &basePath,
                                                const JsonValue &pathValue) {
        if (!pathValue.isString()) {
            return Error{
                Error::InvalidFormat,
                R"(invalid language specification)",
            };
        }
        auto descPath = stdc::path::from_utf8(pathValue.toString());
        if (descPath.empty()) {
            return Error{
                Error::InvalidFormat,
                R"(language specification path has invalid value)",
            };
        }
        if (descPath.is_relative()) {
            descPath = basePath / descPath;
        }

        auto obj = readJsonObjectFile(descPath);
        if (!obj) {
            return obj.error();
        }
        auto exp = read({}, obj.get());
        if (!exp) {
            return exp.error();
        }
        path = fs::canonical(descPath).parent_path();
        return Expected<void>();
    }

    Expected<void> LanguageSpec::Impl::read(const fs::path &basePath, const JsonObject &obj) {
        (void) basePath;
        stdc::VersionNumber fmtVersion_;
        std::string id_;
        std::string className_;

        DisplayText name_;
        int apiLevel_;

        JsonObject schema_;
        JsonObject configuration_;

        {
            const std::set<std::string_view> allowedKeys = {
                "$version", "class", "configuration", "id", "level", "name", "schema",
            };
            for (const auto &item : obj) {
                if (!allowedKeys.count(std::string_view(item.first))) {
                    return Error{
                        Error::InvalidFormat,
                        stdc::formatN(R"(unknown field "%1" in language manifest)", item.first),
                    };
                }
            }
        }

        // $version
        {
            auto it = obj.find("$version");
            if (it == obj.end()) {
                return Error{
                    Error::InvalidFormat,
                    R"(missing "$version" field in language manifest)",
                };
            }
            if (!it->second.isString() || it->second.toString() != "1.0") {
                return Error{
                    Error::FeatureNotSupported,
                    stdc::formatN(R"(format version "%1" is not supported)", it->second.toString()),
                };
            }
            fmtVersion_ = stdc::VersionNumber(1);
        }
        // id
        {
            auto it = obj.find("id");
            if (it == obj.end()) {
                return Error{
                    Error::InvalidFormat,
                    R"(missing "id" field in language manifest)",
                };
            }
            id_ = it->second.toString();
            if (!ContribLocator::isValidSegment(id_)) {
                return Error{
                    Error::InvalidFormat,
                    R"("id" field has invalid value in language manifest)",
                };
            }
        }
        // class
        {
            auto it = obj.find("class");
            if (it == obj.end()) {
                return Error{
                    Error::InvalidFormat,
                    R"(missing "class" field in language manifest)",
                };
            }
            className_ = it->second.toString();
            if (className_.empty()) {
                return Error{
                    Error::InvalidFormat,
                    R"("class" field has invalid value in language manifest)",
                };
            }
        }
        // name
        {
            auto it = obj.find("name");
            if (it != obj.end()) {
                auto exp =
                    DisplayText::fromJsonValue(it->second)
                        .withContext(Error::InvalidFormat,
                                     R"("name" field has invalid value in language manifest)");
                if (!exp) {
                    return exp.error();
                }
                name_ = exp.take();
            }
            if (name_.isEmpty()) {
                name_ = id_;
            }
        }
        // level
        {
            auto it = obj.find("level");
            if (it == obj.end()) {
                return Error{
                    Error::InvalidFormat,
                    R"(missing "level" field in language manifest)",
                };
            }
            apiLevel_ = it->second.toInt();
            if (apiLevel_ == 0) {
                return Error{
                    Error::InvalidFormat,
                    R"("level" field has invalid value in language manifest)",
                };
            }
        }
        // schema
        {
            auto it = obj.find("schema");
            if (it != obj.end()) {
                if (!it->second.isObject()) {
                    return Error{
                        Error::InvalidFormat,
                        R"("schema" field has invalid value in language manifest)",
                    };
                }
                schema_ = it->second.toObject();
            }
        }
        // configuration
        {
            auto it = obj.find("configuration");
            if (it != obj.end()) {
                if (!it->second.isObject()) {
                    return Error{
                        Error::InvalidFormat,
                        R"("configuration" field has invalid value in language manifest)",
                    };
                }
                configuration_ = it->second.toObject();
            }
        }

        fmtVersion = fmtVersion_;
        id = std::move(id_);
        className = std::move(className_);
        name = std::move(name_);
        apiLevel = apiLevel_;
        manifestSchema = std::move(schema_);
        manifestConfiguration = std::move(configuration_);
        return Expected<void>();
    }

    class LanguageCategory::Impl : public ContribCategory::Impl {
    public:
        Impl(LanguageCategory *decl, srt::SynthUnit *su)
            : ContribCategory::Impl(decl, "language", su) {
        }

        std::map<std::string, srt::UNO<LanguageProvider>> providers;
    };

    LanguageSpec::~LanguageSpec() = default;

    const std::string &LanguageSpec::className() const {
        stdc_impl_t;
        return impl.className;
    }

    DisplayText LanguageSpec::name() const {
        stdc_impl_t;
        return impl.name;
    }

    int LanguageSpec::apiLevel() const {
        stdc_impl_t;
        return impl.apiLevel;
    }

    const JsonObject &LanguageSpec::manifestSchema() const {
        stdc_impl_t;
        return impl.manifestSchema;
    }

    LanguageSchema *LanguageSpec::schema() const {
        stdc_impl_t;
        return impl.schema.get();
    }

    const JsonObject &LanguageSpec::manifestConfiguration() const {
        stdc_impl_t;
        return impl.manifestConfiguration;
    }

    LanguageConfiguration *LanguageSpec::configuration() const {
        stdc_impl_t;
        return impl.configuration.get();
    }

    const fs::path &LanguageSpec::path() const {
        stdc_impl_t;
        return impl.path;
    }

    LanguageSpec::LanguageSpec() : ContribSpec(*new Impl()) {
    }

    LanguageCategory::~LanguageCategory() = default;

    std::vector<LanguageSpec *>
        LanguageCategory::findLanguages(const ContribLocator &locator) const {
        stdc_impl_t;
        std::vector<LanguageSpec *> res;
        auto temp = impl.findContributes(locator);
        res.reserve(temp.size());
        for (const auto &item : std::as_const(temp)) {
            res.push_back(static_cast<LanguageSpec *>(item));
        }
        return res;
    }

    std::vector<LanguageSpec *> LanguageCategory::languages() const {
        stdc_impl_t;
        std::shared_lock<std::shared_mutex> lock(impl.su_mtx());
        std::vector<LanguageSpec *> res;
        res.reserve(impl.contributes.size());
        for (const auto &item : impl.contributes) {
            res.push_back(static_cast<LanguageSpec *>(item));
        }
        return res;
    }

    Expected<ContribSpec *> LanguageCategory::parseSpec(const fs::path &basePath,
                                                        const JsonValue &config) const {
        if (!config.isString()) {
            return Error{
                Error::InvalidFormat,
                R"(invalid language specification)",
            };
        }
        auto spec = new LanguageSpec();
        auto spec_impl = static_cast<LanguageSpec::Impl *>(spec->_impl.get());
        if (auto exp = spec_impl->readDesc(basePath, config); !exp) {
            delete spec;
            return exp.error();
        }
        return spec;
    }

    Expected<void> LanguageCategory::loadSpec(ContribSpec *spec, ContribSpec::State state) {
        stdc_impl_t;
        switch (state) {
            case ContribSpec::Initialized: {
                auto langSpec = static_cast<LanguageSpec *>(spec);
                auto spec_impl = static_cast<LanguageSpec::Impl *>(langSpec->_impl.get());

                const auto &key = langSpec->className();
                LanguageProvider *provider = nullptr;

                // Search provider cache
                if (auto it = impl.providers.find(key); it != impl.providers.end()) {
                    provider = it->second.get();
                } else {
                    auto plugin = SU()->plugin<LanguageProviderPlugin>(key.c_str());
                    if (!plugin) {
                        return Error{
                            Error::FeatureNotSupported,
                            stdc::formatN(R"(required provider "%1" of language "%2" not found)",
                                          key, langSpec->id()),
                        };
                    }
                    auto &slot = impl.providers[key];
                    slot = plugin->create();
                    provider = slot.get();
                }

                // Check api level
                if (provider->apiLevel() < langSpec->apiLevel()) {
                    return Error{
                        Error::FeatureNotSupported,
                        stdc::formatN(
                            R"(required provider "%1" of api level %2 doesn't support language "%3" of api level %4)",
                            key, provider->apiLevel(), langSpec->id(), langSpec->apiLevel()),
                    };
                }

                // Create schema and configuration
                auto schema = provider->createSchema(langSpec).withContext(
                    Error::InvalidFormat,
                    stdc::formatN(R"(failed to parse language schema of "%1")", langSpec->id()));
                if (!schema) {
                    return schema.error();
                }
                spec_impl->schema = schema.take();

                auto config = provider->createConfiguration(langSpec).withContext(
                    Error::InvalidFormat,
                    stdc::formatN(R"(failed to parse language configuration of "%1")",
                                  langSpec->id()));
                if (!config) {
                    return config.error();
                }
                spec_impl->configuration = config.take();
                spec_impl->provider = provider;
                return ContribCategory::loadSpec(spec, state);
            }

            case ContribSpec::Ready:
            case ContribSpec::Finished: {
                return Expected<void>();
            }

            case ContribSpec::Deleted: {
                return ContribCategory::loadSpec(spec, state);
            }
            default:
                break;
        }
        return Expected<void>();
    }

    LanguageCategory::LanguageCategory(srt::SynthUnit *su) : ContribCategory(*new Impl(this, su)) {
    }

    static srt::ContribCategoryRegistry::Add<srt::ContribCategoryFactory<LanguageCategory>>
        registrar("language", "Language contributes");

}
