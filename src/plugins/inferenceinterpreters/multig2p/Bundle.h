#ifndef WOLF_MULTIG2P_BUNDLE_H
#define WOLF_MULTIG2P_BUNDLE_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <synthrt/Support/Expected.h>

namespace wolf::multig2p {

    /// Ids of the four reserved symbols of a bundle, located by name rather than by position.
    struct SpecialSymbols {
        std::int64_t unknown = 0;
        std::int64_t padding = 0;
        std::int64_t begin = 0;
        std::int64_t end = 0;
    };

    /// The symbol table with which a bundle was exported.
    ///
    /// Each symbol is written as language/variant/symbol, except the four reserved symbols, which
    /// have no prefix. A lookup occurs once per input byte, so lookups use a hash map rather than a
    /// linear scan.
    class Vocabulary {
    public:
        static srt::Expected<Vocabulary> load(const std::filesystem::path &path);

        /// Returns the id of \a symbol, or -1 if the table does not contain \a symbol.
        std::int64_t lookup(const std::string &symbol) const;

        /// Returns the symbol of \a id without its language/variant prefix, or an empty string if
        /// \a id is out of range.
        std::string phonemeAt(std::int64_t id) const;

        std::size_t size() const noexcept {
            return m_symbols.size();
        }

        const SpecialSymbols &specials() const noexcept {
            return m_specials;
        }

        /// Returns whether \a id is one of the four reserved symbols, which never appear in the
        /// output.
        bool isSpecial(std::int64_t id) const noexcept;

    private:
        std::vector<std::string> m_symbols;
        std::unordered_map<std::string, std::int64_t> m_index;
        SpecialSymbols m_specials;
    };

    /// The contents of bundle.json that describe the models in the same directory.
    ///
    /// The remaining keys (schema_version, model_version, vocab_hash, opset_version, export_flags,
    /// generated_at) are publishing metadata that this contract does not interpret.
    class Bundle {
    public:
        /// The highest bundle_version that this build reads.
        static constexpr int SUPPORTED_VERSION = 1;

        static srt::Expected<Bundle> load(const std::filesystem::path &path);

        /// Returns the file name recorded for the logical model name \a logical, or an
        /// InvalidFormat error if the bundle records no file for \a logical.
        ///
        /// The result is a file name rather than a path: a parsed resource must not capture the
        /// directory from which it was read, because two packages that share a cache entry would
        /// otherwise resolve against the directory of the package that loaded first. The caller
        /// joins the name with its own module directory.
        srt::Expected<std::string> fileName(const std::string &logical) const;

        /// Returns the position of \a ref in the language list of the bundle, or -1 if the list
        /// does not contain \a ref.
        ///
        /// The position is the language id with which the models were trained, so the id is
        /// derived from the list order rather than recorded separately.
        std::int64_t languageId(const std::string &ref) const;

        const std::vector<std::string> &languages() const noexcept {
            return m_languages;
        }

    private:
        std::unordered_map<std::string, std::string> m_files;
        std::vector<std::string> m_languages;
    };

}

#endif // WOLF_MULTIG2P_BUNDLE_H
