#ifndef WOLF_CONTRACTVALUES_H
#define WOLF_CONTRACTVALUES_H

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <synthrt/Core/ContribSpec.h>
#include <synthrt/Support/Expected.h>
#include <synthrt/Support/JSON.h>

#include <wolf/Api/Inferences/Common/1/CommonApiL1.h>

namespace wolf {

    /// Key set of the exports block of one contract.
    ///
    /// The Level 1 contracts of this family compose their exports from the same three elements:
    /// the language pairs that a module produces or consumes, one string set (G2P symbols, S2P and
    /// linguist phonemes, Onset knownPhonemes) and the openSet flag. Each contract uses a subset.
    struct ExportsShape {
        /// Whether the block may carry exports.languages.
        bool languages = false;

        /// The key of the string set, which every contract has.
        const char *strings = nullptr;

        /// Whether the string set must be present. Only the linguist inventory is required.
        bool stringsRequired = false;

        /// Whether the block may carry openSet.
        bool openSet = false;

        /// Whether an absent block is read as empty. Only the linguist exports are required.
        bool optional = true;
    };

    /// The values of one exports block, read according to an ExportsShape.
    struct ContractExports {
        std::vector<Api::Common::L1::LanguageScheme> languages;
        std::vector<std::string> strings;
        bool openSet = false;
    };

    /// Reads the exports block of \a spec.
    ///
    /// A key that the shape does not name is rejected, so that a misspelled key fails the package
    /// instead of silently omitting the intended declaration. A string set specified as a path is
    /// resolved against the directory of the declaration. \a what names the block in error
    /// messages, such as "exports" or "linguist exports".
    srt::Expected<ContractExports> readContractExports(const srt::ContribSpec &spec,
                                                       const ExportsShape &shape,
                                                       std::string_view what);

    /// One entry of a languageMap: a contract pair and the engine or bundle language that serves
    /// the pair.
    struct LanguageMapEntry {
        Api::Common::L1::LanguageScheme pair;
        std::string ref;
    };

    /// Validates a ref according to the rules of the variant that reads the map. \a position names
    /// the entry in error messages.
    using LanguageMapRefCheck =
        std::function<srt::Expected<void>(const std::string &position, const std::string &ref)>;

    /// Reads the languageMap of a configuration: a non-empty array of objects that carry a
    /// language, a scheme and a ref, with no repeated pair.
    ///
    /// If \a checkRef is supplied, it is called for each ref as the ref is read, so that a ref
    /// that the variant cannot serve is rejected at the entry that names it.
    srt::Expected<std::vector<LanguageMapEntry>>
        readLanguageMap(const srt::JsonValue &value, const LanguageMapRefCheck &checkRef = {});

    /// Checks that exports.languages of \a spec and \a languageMap describe the same set of pairs.
    ///
    /// The first is the contract declaration and the second is the implementation declaration.
    /// Neither can be derived from the other, and both are therefore written and reconciled.
    srt::Expected<void> reconcileLanguageMap(const srt::ContribSpec &spec,
                                             const std::vector<LanguageMapEntry> &languageMap);

    /// Reads the formatVersion of a variant configuration.
    ///
    /// A value above \a highest, the highest format version that this build supports, is rejected
    /// with FeatureNotSupported instead of being read leniently. The error message names the
    /// declared value, the supported bound and the required upgrade.
    srt::Expected<int> readFormatVersion(const srt::JsonValue &value, int highest);

}

#endif // WOLF_CONTRACTVALUES_H
