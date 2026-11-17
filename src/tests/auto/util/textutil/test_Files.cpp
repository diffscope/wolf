#include <system_error>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <textutil/Files.h>

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

BOOST_AUTO_TEST_CASE(file_errors_distinguish_missing_from_unreadable_paths) {
    TemporaryDirectory temporary;
    BOOST_CHECK(wolf::fileErrorCode(temporary.path / "missing") == srt::Error::FileNotFound);
    BOOST_CHECK(wolf::fileErrorCode(temporary.path) == srt::Error::FileNotOpen);
    std::ifstream stream;
    auto directory = wolf::openForReading(stream, temporary.path, "dictionary");
    BOOST_REQUIRE(!directory);
    BOOST_CHECK(directory.error().code() == srt::Error::FileNotOpen);
    auto missing = wolf::openForReading(stream, temporary.path / "missing", "dictionary");
    BOOST_REQUIRE(!missing);
    BOOST_CHECK(missing.error().code() == srt::Error::FileNotFound);
    const auto path = temporary.write("data", "first\nsecond");
    BOOST_REQUIRE(wolf::openForReading(stream, path, "dictionary"));
    std::string line;
    BOOST_REQUIRE(static_cast<bool>(std::getline(stream, line)));
    BOOST_CHECK_EQUAL(line, "first");
}

BOOST_AUTO_TEST_CASE(bom_and_cr_are_stripped_only_at_document_boundaries) {
    std::string decorated = "\xef\xbb\xbf"
                            "hello\r";
    wolf::stripLineDecorations(decorated, true);
    BOOST_CHECK_EQUAL(decorated, "hello");
    std::string later = "\xef\xbb\xbf"
                        "hello\r";
    wolf::stripLineDecorations(later, false);
    BOOST_CHECK_EQUAL(later, "\xef\xbb\xbf"
                             "hello");
    std::string empty;
    wolf::stripLineDecorations(empty, true);
    BOOST_CHECK(empty.empty());
}
