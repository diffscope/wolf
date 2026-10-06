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
#include <synthrt/SVS/SingerContrib.h>

#include <wolf/Api/Inferences/Common/1/CommonApiL1.h>
#include <wolf/Api/Inferences/G2P/1/G2PApiL1.h>
#include <wolf/Api/Inferences/Onset/1/OnsetApiL1.h>
#include <wolf/Api/Inferences/S2P/1/S2PApiL1.h>
#include <wolf/Api/Linguists/Linguist/1/LinguistApiL1.h>
#include <wolf/Linguist/LinguistContrib.h>
#include <wolf/Linguist/SingerLanguages.h>
#include <wolf/Support/ContractValues.h>
#include <wolf/Support/Logging.h>
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
            return spec.locator().category() == srt::SingerCategory::NAME;
        }

        bool isLinguistSpec(const srt::ContribSpec &spec) {
            return spec.locator().category() == LINGUIST_CATEGORY;
        }

        /// Returns whether an import is bound to a linguist of the contract that this provider
        /// serves. The extension reads the exports of the target as linguist exports without a
        /// further check, so this function compares the whole contract triple and not only the
        /// category.
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

        /// Validates one entry of the language map of a singer against the contribution that the
        /// entry names.
        ///
        /// The shape of the map key is not checked here. The key must equal the \c language of the
        /// target, which the linguist category has already validated, so a malformed key cannot
        /// match, and a separate rule could only conflict with this one.
        ///
        /// \return Success if the entry is valid; otherwise an error that describes the first
        /// violation.
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

        /// Checks that the language/scheme pair of a linguist is among the pairs that its chain
        /// member declares.
        ///
        /// The two declarations are duals: a G2P module declares the pairs that it produces, an
        /// S2P module declares the pairs that it consumes, and both use the same key. A module that
        /// declares no pairs skips the static check rather than failing, because an empty list is
        /// the only accurate declaration for variants with an open or script-defined output set.
        /// The host issues a warning in that case instead.
        ///
        /// The downcast is unchecked because the provider ABI contract requires the concrete type
        /// to match the triple that the loader has already compared.
        ///
        /// \return Success if the target declares \a pair or declares no pairs. An InvalidFormat
        /// error if the target has no interpreted exports or does not declare \a pair.
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
                        if (item.role() == LinguistApi::ROLE_G2P) {
                            if (auto result =
                                    validateLinguistRole(item, Api::G2P::L1::API_INTERFACE);
                                !result) {
                                return result.takeError();
                            }
                            if (auto result = validateLanguageMatch<Api::G2P::L1::G2PExports>(
                                    item.binding()->target(), pair, LinguistApi::ROLE_G2P);
                                !result) {
                                return result.takeError();
                            }
                            hasG2P = true;
                        } else if (item.role() == LinguistApi::ROLE_S2P) {
                            if (auto result =
                                    validateLinguistRole(item, Api::S2P::L1::API_INTERFACE);
                                !result) {
                                return result.takeError();
                            }
                            if (auto result = validateLanguageMatch<Api::S2P::L1::S2PExports>(
                                    item.binding()->target(), pair, LinguistApi::ROLE_S2P);
                                !result) {
                                return result.takeError();
                            }
                        } else if (item.role() == LinguistApi::ROLE_ONSET) {
                            if (auto result =
                                    validateLinguistRole(item, Api::Onset::L1::API_INTERFACE);
                                !result) {
                                return result.takeError();
                            }
                        } else {
                            // No executive binds this role: the executive creates children for
                            // the three roles only, so a misspelled role such as linguist/onsets
                            // loads and leaves the composition with fewer stages than the
                            // declaration lists. Rejecting the role would change which packages
                            // load; the lint rejects it, and the loader reports a warning.
                            logCategory().srtWarning(
                                "linguist %1 imports the role \"%2\", which the %3 contract "
                                "does not bind; the import is ignored",
                                spec.locator().toString(), item.role(), LinguistApi::API_INTERFACE);
                        }
                    }
                    // Only the g2p role is required. A composition without linguist/s2p is valid
                    // and ends at the pronunciation layer. The ecosystem already uses this
                    // combination when a language package supplies the G2P module and a voicebank
                    // supplies the phoneme stage. The importing variant determines which roles are
                    // required, and the upper specification states that an importer *may*
                    // require a role, not that it must.
                    if (!hasG2P) {
                        return srt::Error(srt::Error::InvalidFormat,
                                          std::string("linguist imports require a ") +
                                              LinguistApi::ROLE_G2P + " role");
                    }
                }
                if (isSingerSpec(spec)) {
                    // The language map of the singer specifies language identity, not the role
                    // names: a role is a local slot name, and the map assigns a language to each
                    // role.
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
                    // The binding is read once, here, because the declaration fixes it, and a
                    // caller that requests it must not need to access the loader.
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
                    // The reachable depth is determined by the import set of the composition, so
                    // it is computed once here rather than detected from a truncated conversion
                    // result.
                    auto depth = LinguistApi::Depth::Pronunciation;
                    // A layer of its own exists when the S2P member converts the symbols, and the
                    // variant of that member decides this. The tables and the scripts of a member
                    // are read during Acquire and are not available to a query that runs while the
                    // composition is only declared, so the variant is the source used here. A
                    // composition without an S2P member reaches phonemes without a layer in
                    // between, which leaves this false.
                    auto separateLayer = false;
                    if (const auto s2p = target.findImport(LinguistApi::ROLE_S2P)) {
                        depth = target.findImport(LinguistApi::ROLE_ONSET)
                                    ? LinguistApi::Depth::Onsets
                                    : LinguistApi::Depth::Phonemes;
                        if (const auto *binding = s2p->binding()) {
                            separateLayer =
                                !Api::S2P::L1::variantKeepsSymbols(binding->target().variant());
                        }
                    }
                    m_depths.emplace(entry.language, depth);
                    m_layers.emplace(entry.language, separateLayer);
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
                // A language that this singer does not declare has no linguist, so its depth is
                // the shallowest value, which is also the value that LanguageStatus reports for
                // an unknown language.
                const auto it = m_depths.find(std::string(language));
                return it == m_depths.end() ? LinguistApi::Depth::Pronunciation : it->second;
            }

            bool hasSeparatePronunciationLayer(std::string_view language) const override {
                // A language that this singer does not declare has no linguist, and a language
                // whose pronunciation is its phoneme layer holds no layer of its own. Both cases
                // report false, which is also the value that LanguageStatus reports for an
                // unknown language.
                const auto it = m_layers.find(std::string(language));
                return it != m_layers.end() && it->second;
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
            /// Whether the pronunciation layer of a language is a layer of its own. See
            /// hasSeparatePronunciationLayer().
            std::map<std::string, bool> m_layers;
            /// Non-owning pointers into the linguist specs, which may belong to other packages. The
            /// package of the singer keeps its resolved dependencies loaded, and this extension is
            /// owned by the spec of the singer, so every referenced object outlives this
            /// extension.
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
        // The validator rejects an entry whose target is not a linguist contribution, so a load
        // that reaches Commit mounts the whole map. The filter remains because the framework does
        // not guarantee that validators run before extensions are created, and the extension
        // reads the exports of the target as linguist exports without a further check.
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
        // Both the phoneme inventory and the exports block that contains it are required.
        constexpr ExportsShape shape{false, "phonemes", true, true, false};
        auto read = readContractExports(spec, shape, "linguist exports");
        if (!read) {
            return read.takeError();
        }
        auto result = std::make_unique<LinguistApi::LinguistExports>();
        result->phonemes = std::move(read->strings);
        result->openSet = read->openSet;
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
