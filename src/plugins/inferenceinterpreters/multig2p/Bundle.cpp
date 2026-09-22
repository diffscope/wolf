#include "Bundle.h"

#include <fstream>
#include <sstream>
#include <utility>

#include <stdcorelib/path.h>

#include <synthrt/Support/JSON.h>

namespace fs = std::filesystem;

namespace wolf::multig2p {

    namespace {

        constexpr char UNKNOWN[] = "<unk>";
        constexpr char PADDING[] = "<pad>";
        constexpr char BEGIN[] = "<bos>";
        constexpr char END[] = "<eos>";

        srt::Expected<srt::JsonValue> readJson(const fs::path &path) {
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                return srt::Error(srt::Error::FileNotFound,
                                  "cannot open " + stdc::path::to_utf8(path));
            }
            std::ostringstream buffer;
            buffer << file.rdbuf();
            stdc::json::ParseError error;
            auto value = srt::JsonValue::fromJson(buffer.str(), false, &error);
            if (error) {
                return srt::Error(srt::Error::InvalidFormat, stdc::path::to_utf8(path) +
                                                                 " is not valid JSON: " +
                                                                 error.message());
            }
            if (!value.isObject()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  stdc::path::to_utf8(path) + " must hold an object");
            }
            return value;
        }

        srt::Expected<std::vector<std::string>> readStrings(const srt::JsonValue &value,
                                                            const std::string &what) {
            if (!value.isArray()) {
                return srt::Error(srt::Error::InvalidFormat, what + " must be an array");
            }
            std::vector<std::string> result;
            for (const auto &item : value.toArray()) {
                if (!item.isString() || item.toString().empty()) {
                    return srt::Error(srt::Error::InvalidFormat,
                                      what + " must hold non-empty strings");
                }
                result.push_back(item.toString());
            }
            return result;
        }

    }

    srt::Expected<Vocabulary> Vocabulary::load(const fs::path &path) {
        auto json = readJson(path);
        if (!json) {
            return json.takeError();
        }
        const auto &object = json->toObject();

        const auto symbols = object.find("symbols");
        if (symbols == object.end()) {
            return srt::Error(srt::Error::InvalidFormat, "the vocabulary needs symbols");
        }
        auto read = readStrings(symbols->second, "vocabulary symbols");
        if (!read) {
            return read.takeError();
        }

        Vocabulary vocabulary;
        vocabulary.m_symbols = read.take();
        if (vocabulary.m_symbols.empty()) {
            return srt::Error(srt::Error::InvalidFormat, "the vocabulary holds no symbols");
        }
        for (std::size_t i = 0; i < vocabulary.m_symbols.size(); ++i) {
            // A repeated symbol would make one of its two ids unreachable, and which one wins
            // would depend on iteration order.
            if (!vocabulary.m_index.emplace(vocabulary.m_symbols[i],
                                            static_cast<std::int64_t>(i))
                     .second) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "the vocabulary repeats a symbol: " + vocabulary.m_symbols[i]);
            }
        }

        // Found by name. The export convention puts them first, but a table that had moved them
        // would otherwise be read with four wrong ids and produce plausible nonsense.
        struct Reserved {
            const char *name;
            std::int64_t SpecialSymbols::*field;
        };
        static constexpr Reserved reserved[] = {
            {UNKNOWN, &SpecialSymbols::unknown},
            {PADDING, &SpecialSymbols::padding},
            {BEGIN, &SpecialSymbols::begin},
            {END, &SpecialSymbols::end},
        };
        for (const auto &entry : reserved) {
            const auto id = vocabulary.lookup(entry.name);
            if (id < 0) {
                return srt::Error(srt::Error::InvalidFormat,
                                  std::string("the vocabulary is missing the reserved symbol ") +
                                      entry.name);
            }
            vocabulary.m_specials.*entry.field = id;
        }
        return vocabulary;
    }

    std::int64_t Vocabulary::lookup(const std::string &symbol) const {
        const auto it = m_index.find(symbol);
        return it == m_index.end() ? -1 : it->second;
    }

    std::string Vocabulary::phonemeAt(std::int64_t id) const {
        if (id < 0 || static_cast<std::size_t>(id) >= m_symbols.size()) {
            return {};
        }
        const auto &symbol = m_symbols[static_cast<std::size_t>(id)];
        const auto slash = symbol.rfind('/');
        return slash == std::string::npos ? symbol : symbol.substr(slash + 1);
    }

    bool Vocabulary::isSpecial(std::int64_t id) const noexcept {
        return id == m_specials.unknown || id == m_specials.padding || id == m_specials.begin ||
               id == m_specials.end;
    }

    srt::Expected<Bundle> Bundle::load(const fs::path &path) {
        auto json = readJson(path);
        if (!json) {
            return json.takeError();
        }
        const auto &object = json->toObject();

        // The bundle's own generation, handled the way every other resource format version is:
        // a build refuses what it cannot read rather than reading it leniently.
        const auto version = object.find("bundle_version");
        if (version == object.end() || !version->second.isInt() || version->second.toInt() < 1) {
            return srt::Error(srt::Error::InvalidFormat,
                              "the bundle needs a positive integer bundle_version");
        }
        if (version->second.toInt() > SUPPORTED_VERSION) {
            return srt::Error(srt::Error::FeatureNotSupported,
                              "this bundle declares version " +
                                  std::to_string(version->second.toInt()) + ", above the " +
                                  std::to_string(SUPPORTED_VERSION) +
                                  " this build reads; upgrade wolf to load it");
        }

        Bundle bundle;

        const auto files = object.find("files");
        if (files == object.end() || !files->second.isObject()) {
            return srt::Error(srt::Error::InvalidFormat, "the bundle needs a files object");
        }
        for (const auto &[logical, name] : files->second.toObject()) {
            if (!name.isString() || name.toString().empty()) {
                return srt::Error(srt::Error::InvalidFormat,
                                  "the bundle file " + logical + " must name a file");
            }
            bundle.m_files.emplace(logical, name.toString());
        }

        const auto languages = object.find("languages");
        if (languages == object.end()) {
            return srt::Error(srt::Error::InvalidFormat, "the bundle needs a languages list");
        }
        auto read = readStrings(languages->second, "bundle languages");
        if (!read) {
            return read.takeError();
        }
        bundle.m_languages = read.take();
        if (bundle.m_languages.empty()) {
            return srt::Error(srt::Error::InvalidFormat, "the bundle lists no languages");
        }
        return bundle;
    }

    srt::Expected<std::string> Bundle::fileName(const std::string &logical) const {
        const auto it = m_files.find(logical);
        if (it == m_files.end()) {
            return srt::Error(srt::Error::InvalidFormat,
                              "the bundle does not name a " + logical + " model");
        }
        return it->second;
    }

    std::int64_t Bundle::languageId(const std::string &ref) const {
        for (std::size_t i = 0; i < m_languages.size(); ++i) {
            if (m_languages[i] == ref) {
                return static_cast<std::int64_t>(i);
            }
        }
        return -1;
    }

}
