#include <system_error>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <pinyin/PinyinEngines.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {

    struct TemporaryDirectory {
        std::filesystem::path path =
            std::filesystem::temp_directory_path() /
            ("class-tests-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        TemporaryDirectory() {
            std::filesystem::create_directories(path);
        }
        ~TemporaryDirectory() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
        std::filesystem::path write(const std::string &name, const std::string &text) {
            const auto result = path / name;
            std::ofstream stream(result, std::ios::binary);
            stream << text;
            BOOST_REQUIRE(stream.good());
            return result;
        }
    };

}

BOOST_AUTO_TEST_CASE(registry_rejects_unknown_dialects_and_missing_dictionaries) {
    BOOST_CHECK(wolf::pinyin::PinyinEngineRegistry::isKnownRef("mandarin"));
    BOOST_CHECK(wolf::pinyin::PinyinEngineRegistry::isKnownRef("cantonese"));
    BOOST_CHECK(!wolf::pinyin::PinyinEngineRegistry::isKnownRef("unknown"));
    TemporaryDirectory temporary;
    auto unknown = wolf::pinyin::PinyinEngineRegistry::createEngine(temporary.path, "unknown");
    BOOST_REQUIRE(!unknown);
    BOOST_CHECK(unknown.error().code() == srt::Error::FeatureNotSupported);
    for (const auto &dialect : {"mandarin", "cantonese"}) {
        auto missing = wolf::pinyin::PinyinEngineRegistry::createEngine(temporary.path, dialect);
        BOOST_REQUIRE(!missing);
        BOOST_CHECK(missing.error().code() == srt::Error::FileNotFound);
    }
}
