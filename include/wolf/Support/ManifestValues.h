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
#include <wolf/wolf_global.h>

namespace wolf {

    /// Reads a set of strings written either inline or as a path to a JSON array.
    ///
    /// Four contract keys share this shape: the linguist phoneme inventory, S2P phonemes, G2P
    /// symbols and Onset knownPhonemes. Entries must be non-empty and must not repeat.
    ///
    /// A relative path resolves against \a base, which is the directory of the declaration file the
    /// value came from. Variable expansion has already happened by the time a value reaches here.
    ///
    /// \a what names the key in error messages.
    WOLF_EXPORT srt::Expected<std::vector<std::string>>
        readStringSet(const srt::JsonValue &value, const std::filesystem::path &base,
                      std::string_view what);

    /// Reads a declared path the way the specification defines one.
    ///
    /// Spec 2.4 counts `/` and `\` as separators and asks the reader to split and normalize before
    /// the host file system sees the string, so that one declaration names the same file on a host
    /// where only `/` separates. This is that step: it rewrites `\` and hands the result to
    /// \c stdc::path::from_utf8. Every reader of a declared path goes through here rather than
    /// carrying its own copy of the rule, which is what keeps the meaning of a path from changing
    /// with the platform.
    ///
    /// Resolution and any further normalization stay with the caller: the readers differ in whether
    /// they accept an absolute path, in the base they resolve against, and in whether they
    /// normalize the result at all.
    WOLF_EXPORT std::filesystem::path pathFromManifest(std::string_view text);

    /// Matches a language handle: an ISO 639-3 code, three ASCII lowercase letters.
    ///
    /// The identity fields and the pairs inside exports answer to the same grammar, so both ask
    /// here rather than each carrying its own copy of the rule.
    WOLF_EXPORT bool isLanguageHandle(std::string_view value);

    /// Matches a scheme name: [a-z0-9]+( "-" [a-z0-9]+ )*
    WOLF_EXPORT bool isSchemeName(std::string_view value);

    /// Reads an optional boolean. Absent means \a fallback; anything that is not a boolean is an
    /// error rather than a silent false, because a misspelt value would otherwise read as the
    /// safer of the two answers when it is the more dangerous one.
    WOLF_EXPORT srt::Expected<bool> readFlag(const srt::JsonObject &object, std::string_view key,
                                             bool fallback, std::string_view what);

    /// Accepts the import options of a Level 1 contract, all of which define none: the value
    /// must be absent or an empty object. Written once so that every interpreter refuses a
    /// misplaced option the same way instead of ignoring what it was handed.
    WOLF_EXPORT srt::Expected<void> requireNoImportOptions(const srt::JsonValue &value,
                                                           std::string_view what);

    /// Rejects an object carrying a key the contract does not define.
    ///
    /// These objects have a published schema, and a key outside it is a misspelling far more
    /// often than an extension: the declaration then reads as if it said something it did not,
    /// and the part it meant to say is silently absent.
    ///
    /// \a what names the object in error messages.
    WOLF_EXPORT srt::Expected<void> rejectUnknownKeys(const srt::JsonObject &object,
                                                      std::initializer_list<const char *> allowed,
                                                      std::string_view what);

    /// Reads an array of language and scheme pairs.
    ///
    /// This is the shape of the exports.languages key that G2P declares as what it can produce and
    /// S2P as what it can consume. Pairs must not repeat.
    WOLF_EXPORT srt::Expected<std::vector<Api::Common::L1::LanguageScheme>>
        readLanguageSchemes(const srt::JsonValue &value, std::string_view what);

}

#endif // WOLF_MANIFESTVALUES_H
