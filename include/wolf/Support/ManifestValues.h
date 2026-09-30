#ifndef WOLF_MANIFESTVALUES_H
#define WOLF_MANIFESTVALUES_H

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include <synthrt/Support/Expected.h>
#include <synthrt/Support/JSON.h>

#include <wolf/Api/Inferences/Common/1/CommonApiL1.h>

namespace wolf {

    /// Reads a string set specified either inline or as a path to a JSON array.
    ///
    /// Four contract keys share this shape: the linguist phoneme inventory, S2P phonemes, G2P
    /// symbols and Onset knownPhonemes. Entries must be non-empty and must not repeat.
    ///
    /// A relative path is resolved against \a base, the directory of the declaration file that
    /// contains the value. Variable expansion is complete before this function is called.
    ///
    /// \a what names the key in error messages.
    srt::Expected<std::vector<std::string>> readStringSet(const srt::JsonValue &value,
                                                          const std::filesystem::path &base,
                                                          std::string_view what);

    /// Reads a declared path as the specification defines it.
    ///
    /// Spec 2.4 treats `/` and `\` as separators and requires the reader to split and normalize the
    /// string before passing it to the host file system, so that one declaration names the same
    /// file on a host that recognizes only `/` as a separator. This function implements that step:
    /// it rewrites `\` and passes the result to \c stdc::path::from_utf8. Every reader of a
    /// declared path calls this function instead of implementing the rule separately, so that the
    /// meaning of a path does not depend on the platform.
    ///
    /// Resolution and further normalization remain the responsibility of the caller, because the
    /// readers differ in whether they accept an absolute path, in the base against which they
    /// resolve, and in whether they normalize the result.
    std::filesystem::path pathFromManifest(std::string_view text);

    /// Matches a language handle: an ISO 639-3 code, three ASCII lowercase letters.
    ///
    /// The identity fields and the pairs inside exports follow the same grammar, and both are
    /// validated here instead of by separate copies of the rule.
    bool isLanguageHandle(std::string_view value);

    /// Matches a scheme name: [a-z0-9]+( "-" [a-z0-9]+ )*
    bool isSchemeName(std::string_view value);

    /// Reads an optional boolean. An absent value yields \a fallback. A value that is not a
    /// boolean is an error instead of an implicit false, because a misspelled value would
    /// otherwise be read as the apparently safer value although it may be the more dangerous one.
    srt::Expected<bool> readFlag(const srt::JsonObject &object, std::string_view key, bool fallback,
                                 std::string_view what);

    /// Validates the import options of a Level 1 contract. No Level 1 contract defines import
    /// options, and the value must therefore be an empty object. The loader supplies an empty
    /// object if the declaration omits the key, and an explicit null, like any other value that is
    /// not an object, is rejected. A single implementation ensures that every interpreter rejects
    /// a misplaced option in the same way instead of ignoring it.
    srt::Expected<void> requireNoImportOptions(const srt::JsonValue &value, std::string_view what);

    /// Rejects an object that contains a key the contract does not define.
    ///
    /// These objects have a published schema, and a key outside the schema is far more often a
    /// misspelling than an extension. An accepted misspelling would silently omit the intended
    /// declaration.
    ///
    /// \a what names the object in error messages.
    srt::Expected<void> rejectUnknownKeys(const srt::JsonObject &object,
                                          std::initializer_list<const char *> allowed,
                                          std::string_view what);

    /// Reads an array of language and scheme pairs.
    ///
    /// This is the structure of the exports.languages key, which declares the output pairs of G2P
    /// and the input pairs of S2P. Pairs must not repeat.
    srt::Expected<std::vector<Api::Common::L1::LanguageScheme>>
        readLanguageSchemes(const srt::JsonValue &value, std::string_view what);

}

#endif // WOLF_MANIFESTVALUES_H
