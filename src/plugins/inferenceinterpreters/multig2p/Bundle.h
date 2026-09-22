#ifndef WOLF_MULTIG2P_BUNDLE_H
#define WOLF_MULTIG2P_BUNDLE_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <synthrt/Support/Expected.h>

namespace wolf::multig2p {

    /// The four symbols every bundle reserves, found by name rather than by position.
    struct SpecialSymbols {
        std::int64_t unknown = 0;
        std::int64_t padding = 0;
        std::int64_t begin = 0;
        std::int64_t end = 0;
    };

    /// The symbol table a bundle was exported with.
    ///
    /// Symbols are written as language/variant/symbol, with the four specials bare. Lookups happen
    /// once per input character, so they go through a map rather than a scan.
    class Vocabulary {
    public:
        static srt::Expected<Vocabulary> load(const std::filesystem::path &path);

        /// Returns the id of \a symbol, or -1 when the table has no such symbol.
        std::int64_t lookup(const std::string &symbol) const;

        /// Returns the symbol of \a id stripped of its language prefix, or empty when out of range.
        std::string phonemeAt(std::int64_t id) const;

        std::size_t size() const noexcept {
            return m_symbols.size();
        }

        const SpecialSymbols &specials() const noexcept {
            return m_specials;
        }

        /// Whether \a id is one of the four specials, which never reach the output.
        bool isSpecial(std::int64_t id) const noexcept;

    private:
        std::vector<std::string> m_symbols;
        std::unordered_map<std::string, std::int64_t> m_index;
        SpecialSymbols m_specials;
    };

    /// What bundle.json says about the models beside it.
    ///
    /// The other keys it carries (schema_version, model_version, vocab_hash, opset_version,
    /// export_flags, generated_at) are publishing metadata that this contract does not interpret.
    class Bundle {
    public:
        /// The highest resource generation this build reads.
        static constexpr int SUPPORTED_VERSION = 1;

        static srt::Expected<Bundle> load(const std::filesystem::path &path);

        /// Returns the file name recorded for \a logical.
        ///
        /// A name, not a path: a parsed resource may not capture the directory it was read from,
        /// or two packages sharing a cache entry would resolve against whichever loaded first.
        /// The caller joins it with the module directory it already has.
        srt::Expected<std::string> fileName(const std::string &logical) const;

        /// Returns the position of \a ref in the bundle's language list, or -1 when absent.
        ///
        /// That position is the language id the models were trained with, so it is derived from
        /// the declaration order rather than written down twice.
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
