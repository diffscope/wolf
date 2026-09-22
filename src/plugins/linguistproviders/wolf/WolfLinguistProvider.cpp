#include "WolfLinguistProvider.h"

#include "LinguistExecutiveImpl.h"
#include "WolfPipelineExecutive.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <stdcorelib/path.h>

#include <synthrt/Core/ContribImportBinding.h>

#include <wolf/Api/Inferences/Common/1/CommonApiL1.h>
#include <wolf/Api/Inferences/G2P/1/G2PApiL1.h>
#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>
#include <wolf/Api/Inferences/S2P/1/S2PApiL1.h>
#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>
#include <wolf/Linguist/LinguistContrib.h>
#include <wolf/Linguist/SingerLanguages.h>
#include <wolf/Support/ManifestValues.h>

namespace fs = std::filesystem;

namespace wolf {

    namespace LinguistApi = Api::Linguist::L1;

    namespace {

        class LinguistExecutiveFactory : public srt::ContribExecutiveFactory {
        public:
            explicit LinguistExecutiveFactory(srt::ContribImportBinding &binding)
                : m_binding(&binding) {
            }

            srt::Expected<std::unique_ptr<srt::ContribExecutive>>
                create(const srt::ContribRuntimeOptions &runtimeOptions) override {
                if (runtimeOptions.interface() != LinguistApi::API_INTERFACE ||
                    runtimeOptions.variant() != LinguistApi::API_VARIANT ||
                    runtimeOptions.level() != LinguistApi::API_LEVEL) {
                    return srt::Error(
                        srt::Error::InvalidArgument,
                        "linguist runtime options have an incompatible contract identity");
                }
                return std::unique_ptr<srt::ContribExecutive>(
                    new LinguistExecutiveImpl(*m_binding->target().as<wolf::LinguistSpec>()));
            }

        private:
            srt::ContribImportBinding *m_binding;
        };

        bool isSingerSpec(const srt::ContribSpec &spec) {
            return spec.locator().category() == "singer";
        }

        bool isLinguistSpec(const srt::ContribSpec &spec) {
            return spec.locator().category() == LINGUIST_CATEGORY;
        }

        /// Whether an import leads to a linguist of the contract this provider serves. The
        /// extension reads the target's exports as linguist exports without a further check, so
        /// this asks about the whole triple and not only the category.
        bool isLinguistTarget(const srt::ContribImport &item) {
            if (!item.binding()) {
                return false;
            }
            const auto &target = item.binding()->target();
            return target.locator().category() == LINGUIST_CATEGORY &&
                   target.interface() == LinguistApi::API_INTERFACE &&
                   target.variant() == LinguistApi::API_VARIANT &&
                   target.level() == LinguistApi::API_LEVEL;
        }

        /// Validates one entry of a singer's language map against the contribution it names.
        ///
        /// The map key is not shape checked here. It has to equal the target's own \c language,
        /// which the linguist category already validated, so a malformed key cannot match and a
        /// separate rule could only ever disagree with this one.
        srt::Expected<void> validateSingerLanguage(const srt::ContribSpec &singer,
                                                   const SingerLanguage &entry) {
            const auto import = singer.findImport(entry.role);
            if (!import || !import->binding()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "singer language " + entry.language + " has no prepared binding");
            }
            const auto &target = import->binding()->target();
            if (target.locator().category() != LINGUIST_CATEGORY) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "singer language " + entry.language +
                                      " names an import that is not a linguist contribution");
            }
            if (target.interface() != LinguistApi::API_INTERFACE ||
                target.variant() != LinguistApi::API_VARIANT ||
                target.level() != LinguistApi::API_LEVEL) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "singer linguist import has an incompatible contract identity");
            }
            if (!import->executiveFactory()) {
                return srt::Error(srt::Error::FeatureNotSupported,
                                  "linguist import has no execution factory");
            }
            if (target.as<LinguistSpec>()->language() != entry.language) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "singer language " + entry.language +
                                      " names a contribution whose language is " +
                                      target.as<LinguistSpec>()->language());
            }
            return {};
        }

        /// Checks that a linguist's own pair is among those its chain member declares.
        ///
        /// The two declarations are duals: G2P says which pairs it can produce, S2P which it can
        /// consume, and both use the same key. A module that declares none forfeits the static
        /// match rather than failing, which is the only way variants with an open or scripted
        /// output set can be declared honestly; the host warns instead.
        ///
        /// The downcast is unchecked by the provider ABI contract, which requires the concrete type
        /// to match the triple the loader already compared.
        template <class Exports>
        srt::Expected<void> validateLanguageMatch(const srt::ContribSpec &target,
                                                  const Api::Common::L1::LanguageScheme &pair,
                                                  std::string_view what) {
            auto exports = target.exports();
            if (!exports) {
                return srt::Error(srt::Error::InvalidFormat,
                                  std::string(what) + " target has no interpreted exports");
            }
            const auto &declared = exports->as<Exports>()->languages;
            if (declared.empty()) {
                return {};
            }
            if (std::find(declared.begin(), declared.end(), pair) == declared.end()) {
                return srt::Error(srt::Error::InvalidFormat, "the " + std::string(what) +
                                                                 " target does not declare " +
                                                                 pair.language + "/" + pair.scheme);
            }
            return {};
        }

        srt::Expected<void> validateLinguistRole(const srt::ContribImport &item,
                                                 std::string_view expectedInterface) {
            if (!item.binding()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "linguist import has no prepared binding");
            }
            if (item.binding()->target().interface() != expectedInterface) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "linguist import role targets an incompatible interface");
            }
            if (!item.executiveFactory()) {
                return srt::Error(srt::Error::FeatureNotSupported,
                                  "linguist import has no execution factory");
            }
            return {};
        }

        class WolfImportValidator : public srt::ContribImportValidator {
        public:
            srt::Expected<void> validateImports(const srt::ContribSpec &spec) const override {
                if (isLinguistSpec(spec)) {
                    auto linguist = spec.as<LinguistSpec>();
                    const Api::Common::L1::LanguageScheme pair(linguist->language(),
                                                               linguist->scheme());
                    bool hasG2P = false;
                    for (const auto &item : spec.imports()) {
                        if (item.role() == "linguist/g2p") {
                            if (auto result =
                                    validateLinguistRole(item, Api::G2P::L1::API_INTERFACE);
                                !result) {
                                return result.takeError();
                            }
                            if (auto result = validateLanguageMatch<Api::G2P::L1::G2PExports>(
                                    item.binding()->target(), pair, "linguist/g2p");
                                !result) {
                                return result.takeError();
                            }
                            hasG2P = true;
                        } else if (item.role() == "linguist/s2p") {
                            if (auto result =
                                    validateLinguistRole(item, Api::S2P::L1::API_INTERFACE);
                                !result) {
                                return result.takeError();
                            }
                            if (auto result = validateLanguageMatch<Api::S2P::L1::S2PExports>(
                                    item.binding()->target(), pair, "linguist/s2p");
                                !result) {
                                return result.takeError();
                            }
                        } else if (item.role() == "linguist/onset") {
                            if (auto result =
                                    validateLinguistRole(item, Api::Onset::L1::API_INTERFACE);
                                !result) {
                                return result.takeError();
                            }
                        }
                    }
                    // Only g2p is required. A composition without linguist/s2p is a legal shape
                    // rather than a broken one: it reaches the pronunciation layer and no further,
                    // which is exactly the combination the ecosystem already uses when a language
                    // package supplies the G2P and a voicebank supplies the phoneme stage. Which
                    // roles must exist is the importing variant's own decision, and the upper
                    // specification says an importer *may* insist rather than must.
                    if (!hasG2P) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          "linguist imports require a linguist/g2p role");
                    }
                }
                if (isSingerSpec(spec)) {
                    // Language identity lives in the singer's language map, not in role names: a
                    // role is a local slot, and every language it can carry is named by the map.
                    auto languages = readSingerLanguages(spec);
                    if (!languages) {
                        return languages.takeError();
                    }
                    for (const auto &entry : languages->entries) {
                        if (auto result = validateSingerLanguage(spec, entry); !result) {
                            return result.takeError();
                        }
                    }
                }
                return {};
            }
        };

        class WolfPipelineExtension : public LinguistApi::WolfPipelineExtension {
        public:
            WolfPipelineExtension(srt::SingerSpec &spec, SingerLanguages languages)
                : LinguistApi::WolfPipelineExtension(
                      spec,
                      srt::ContribSpecExtensionTraits<srt::SingerSpec,
                                                      LinguistApi::WolfPipelineExecutive>::ID),
                  m_languages(std::move(languages)) {
                m_handles.reserve(m_languages.entries.size());
                for (const auto &entry : m_languages.entries) {
                    m_handles.push_back(entry.language);
                    // Read once, here: the binding is fixed by the declaration, and a caller
                    // asking for it must not have to reach back into the loader.
                    const auto import = spec.findImport(entry.role);
                    if (!import || !import->binding()) {
                        continue;
                    }
                    const auto &target = import->binding()->target();
                    if (auto linguist = target.as<LinguistSpec>()) {
                        m_bindings.emplace(entry.language,
                                           Api::Common::L1::LanguageScheme(linguist->language(),
                                                                           linguist->scheme()));
                    }
                    // The reachable depth is a fact about the composition's import set, so it is
                    // read once here rather than discovered by converting and finding the answer
                    // short.
                    auto depth = LinguistApi::Depth::Pronunciation;
                    if (target.findImport("linguist/s2p")) {
                        depth = target.findImport("linguist/onset") ? LinguistApi::Depth::Onsets
                                                                    : LinguistApi::Depth::Phonemes;
                    }
                    m_depths.emplace(entry.language, depth);
                    if (auto values = target.exports()) {
                        m_exports.emplace(entry.language,
                                          values->as<LinguistApi::LinguistExports>());
                    }
                }
            }

            const std::vector<std::string> &languages() const override {
                return m_handles;
            }

            const std::string &defaultLanguage() const override {
                return m_languages.defaultLanguage;
            }

            const srt::ContribLocator *locate(std::string_view language) const override {
                for (const auto &entry : m_languages.entries) {
                    if (entry.language != language) {
                        continue;
                    }
                    const auto import = spec().findImport(entry.role);
                    if (import && import->binding()) {
                        return &import->binding()->target().locator();
                    }
                }
                return nullptr;
            }

            const Api::Common::L1::LanguageScheme *
                binding(std::string_view language) const override {
                const auto it = m_bindings.find(std::string(language));
                return it == m_bindings.end() ? nullptr : &it->second;
            }

            LinguistApi::Depth maxDepth(std::string_view language) const override {
                // A language this singer does not declare reaches nothing, so its depth is the
                // shallowest, the same answer LanguageStatus gives for what it cannot see.
                const auto it = m_depths.find(std::string(language));
                return it == m_depths.end() ? LinguistApi::Depth::Pronunciation : it->second;
            }

            const LinguistApi::LinguistExports *exports(std::string_view language) const override {
                const auto it = m_exports.find(std::string(language));
                return it == m_exports.end() ? nullptr : it->second;
            }

            srt::Expected<std::unique_ptr<srt::SingerPipelineExecutive>>
                createPipeline(const srt::SingerPipelineRuntimeOptions &runtimeOptions) override {
                if (runtimeOptions.interface() != LinguistApi::API_INTERFACE ||
                    runtimeOptions.variant() != LinguistApi::API_VARIANT ||
                    runtimeOptions.level() != LinguistApi::API_LEVEL) {
                    return srt::Error(
                        srt::Error::InvalidArgument,
                        "wolf pipeline options have an incompatible contract identity");
                }
                return std::unique_ptr<srt::SingerPipelineExecutive>(
                    new WolfPipelineExecutive(spec(), m_languages));
            }

        private:
            SingerLanguages m_languages;
            std::vector<std::string> m_handles;
            std::map<std::string, Api::Common::L1::LanguageScheme> m_bindings;
            std::map<std::string, LinguistApi::Depth> m_depths;
            std::map<std::string, const LinguistApi::LinguistExports *> m_exports;
        };

    }

    WolfLinguistProvider::WolfLinguistProvider() = default;

    WolfLinguistProvider::~WolfLinguistProvider() = default;

    srt::Expected<std::vector<std::unique_ptr<srt::ContribImportValidator>>>
        WolfLinguistProvider::createImportValidators() const {
        std::vector<std::unique_ptr<srt::ContribImportValidator>> result;
        result.emplace_back(new WolfImportValidator());
        return result;
    }

    srt::Expected<std::vector<std::unique_ptr<srt::ContribSpecExtension>>>
        WolfLinguistProvider::createExtensions(srt::ContribSpec &spec) const {
        std::vector<std::unique_ptr<srt::ContribSpecExtension>> result;
        if (!isSingerSpec(spec)) {
            return result;
        }
        auto languages = readSingerLanguages(spec);
        if (!languages) {
            return languages.takeError();
        }
        // The validator rejects an entry that leads anywhere but a linguist contribution, so a
        // load that reaches Commit mounts the whole map. The filter stays because the framework
        // does not promise that validators run before extensions are created, and the extension
        // reads the target's exports as linguist exports without a further check.
        SingerLanguages mounted;
        mounted.defaultLanguage = languages->defaultLanguage;
        for (auto &entry : languages->entries) {
            const auto import = spec.findImport(entry.role);
            if (import && isLinguistTarget(*import)) {
                mounted.entries.push_back(std::move(entry));
            }
        }
        if (!mounted.empty()) {
            result.emplace_back(
                new WolfPipelineExtension(*spec.as<srt::SingerSpec>(), std::move(mounted)));
        }
        return result;
    }

    srt::Expected<std::unique_ptr<srt::ContribImportOptions>>
        WolfLinguistProvider::createImportOptions(const srt::ContribSpec &target,
                                                  const srt::JsonValue &manifestOptions) const {
        if (auto checked = requireNoImportOptions(manifestOptions, "linguist"); !checked) {
            return checked.takeError();
        }
        if (target.interface() != LinguistApi::API_INTERFACE ||
            target.variant() != LinguistApi::API_VARIANT ||
            target.level() != LinguistApi::API_LEVEL) {
            return srt::Error(srt::Error::InvalidArgument,
                              "linguist import target has an unsupported contract");
        }
        return std::unique_ptr<srt::ContribImportOptions>(new LinguistApi::LinguistImportOptions());
    }

    srt::Expected<std::unique_ptr<srt::ContribExecutiveFactory>>
        WolfLinguistProvider::createExecutiveFactory(srt::ContribImportBinding &binding) const {
        return std::unique_ptr<srt::ContribExecutiveFactory>(new LinguistExecutiveFactory(binding));
    }

    srt::Expected<std::unique_ptr<srt::ContribExports>>
        WolfLinguistProvider::createExports(const srt::ContribSpec &spec) const {
        if (!spec.manifestExports().isObject()) {
            return srt::Error(srt::Error::InvalidFormat, "linguist exports must be an object");
        }
        const auto &object = spec.manifestExports().toObject();
        if (auto checked = rejectUnknownKeys(object, {"phonemes", "openSet"}, "linguist exports");
            !checked) {
            return checked.takeError();
        }
        const auto it = object.find("phonemes");
        if (it == object.end()) {
            return srt::Error(srt::Error::InvalidFormat, "linguist exports require phonemes");
        }
        auto phonemes = readStringSet(it->second, spec.declarationPath().parent_path(),
                                      "linguist exports phonemes");
        if (!phonemes) {
            return phonemes.takeError();
        }
        auto openSet = readFlag(object, "openSet", false, "linguist exports");
        if (!openSet) {
            return openSet.takeError();
        }
        auto result = std::make_unique<LinguistApi::LinguistExports>();
        result->phonemes = phonemes.take();
        result->openSet = openSet.take();
        return std::unique_ptr<srt::ContribExports>(std::move(result));
    }

    srt::Expected<std::unique_ptr<srt::ContribConfiguration>>
        WolfLinguistProvider::createConfiguration(const srt::ContribSpec &spec) const {
        if (!spec.manifestConfiguration().isObject()) {
            return srt::Error(srt::Error::InvalidFormat,
                              "linguist configuration must be an object");
        }
        if (!spec.manifestConfiguration().toObject().empty()) {
            return srt::Error(srt::Error::InvalidFormat,
                              "the wolf linguist configuration must be empty at Level 1");
        }
        return std::unique_ptr<srt::ContribConfiguration>(new LinguistApi::LinguistConfiguration());
    }

}
