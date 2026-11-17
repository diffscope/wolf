#include <system_error>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <lua/LuaSandbox.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(sandbox_removes_external_access_and_checks_return_shapes) {
    auto created = wolf::lua::Sandbox::create(R"(
        function restricted(x)
            assert(io == nil and os == nil and debug == nil and require == nil)
            return {x, "safe"}
        end
        function flags(x) return {true, false} end
        function wrong(x) return {42} end
        function short(x) return {} end
    )",
                                              "test-sandbox");
    BOOST_REQUIRE(created);
    auto sandbox = created.take();
    BOOST_CHECK(sandbox->hasFunction("restricted"));
    BOOST_CHECK(!sandbox->hasFunction("missing"));
    auto strings = sandbox->callForStrings("restricted", "word");
    BOOST_REQUIRE(strings);
    BOOST_CHECK(strings.value() == std::vector<std::string>({"word", "safe"}));
    auto flags = sandbox->callForFlags("flags", {"a", "b"});
    BOOST_REQUIRE(flags);
    BOOST_CHECK(flags.value() == std::vector<bool>({true, false}));
    auto wrong = sandbox->callForStrings("wrong", "a");
    BOOST_REQUIRE(!wrong);
    BOOST_CHECK(wrong.error().code() == srt::Error::InvalidFormat);
    auto shortResult = sandbox->callForFlags("short", {"a"});
    BOOST_REQUIRE(!shortResult);
    BOOST_CHECK(shortResult.error().code() == srt::Error::InvalidFormat);
    auto invalid = wolf::lua::Sandbox::create("function (", "broken");
    BOOST_REQUIRE(!invalid);
    BOOST_CHECK(invalid.error().code() == srt::Error::InvalidFormat);
}

BOOST_AUTO_TEST_CASE(failed_calls_restore_the_stack_and_preserve_binary_strings) {
    auto created = wolf::lua::Sandbox::create(R"(
        function scalar(x) return 1 end
        function wrongStrings(x) return {"first", false} end
        function wrongFlags(x) return {true, "false"} end
        function longFlags(x) return {true, false, true} end
        function throws(x) error("test refusal") end
        function echo(x) return {x, string.char(0, 65, 0)} end
        function flags(x) return {x[1] == string.char(65, 0, 66), x[2] == ""} end
    )",
                                              "failure-recovery");
    BOOST_REQUIRE(created);
    auto sandbox = created.take();
    const std::string binary("A\0B", 3);
    for (int repeat = 0; repeat < 8; ++repeat) {
        for (const auto name : {"missing", "scalar", "wrongStrings", "throws"}) {
            auto failed = sandbox->callForStrings(name, binary);
            BOOST_REQUIRE(!failed);
            BOOST_CHECK(failed.error().code() == srt::Error::InvalidFormat);
        }
        for (const auto name : {"missing", "scalar", "wrongFlags", "longFlags", "throws"}) {
            auto failed = sandbox->callForFlags(name, {binary, ""});
            BOOST_REQUIRE(!failed);
            BOOST_CHECK(failed.error().code() == srt::Error::InvalidFormat);
        }
        BOOST_CHECK(!sandbox->hasFunction("missing"));
        auto echoed = sandbox->callForStrings("echo", binary);
        BOOST_REQUIRE(echoed);
        BOOST_REQUIRE_EQUAL(echoed->size(), 2u);
        BOOST_CHECK_EQUAL((*echoed)[0], binary);
        BOOST_CHECK_EQUAL((*echoed)[1], std::string("\0A\0", 3));
        auto flags = sandbox->callForFlags("flags", {binary, ""});
        BOOST_REQUIRE(flags);
        BOOST_CHECK(*flags == std::vector<bool>({true, true}));
    }
}

BOOST_AUTO_TEST_CASE(sandboxes_own_separate_globals_and_resume_after_an_interrupt) {
    const std::string source = R"(
        counter = 0
        function count(x)
            local total = 0
            for i = 1, 100000 do total = total + i end
            counter = counter + 1
            return {tostring(counter)}
        end
    )";
    auto first = wolf::lua::Sandbox::create(source, "first");
    auto second = wolf::lua::Sandbox::create(source, "second");
    BOOST_REQUIRE(first);
    BOOST_REQUIRE(second);
    (*first)->interrupt();
    auto canceled = (*first)->callForStrings("count", "");
    BOOST_REQUIRE(!canceled);
    BOOST_CHECK(canceled.error().message().find("stopped") != std::string::npos);
    (*first)->resume();
    for (const auto expected : {"1", "2"}) {
        auto result = (*first)->callForStrings("count", "");
        BOOST_REQUIRE(result);
        BOOST_CHECK(*result == std::vector<std::string>({expected}));
    }
    auto isolated = (*second)->callForStrings("count", "");
    BOOST_REQUIRE(isolated);
    BOOST_CHECK(*isolated == std::vector<std::string>({"1"}));
    auto failed = wolf::lua::Sandbox::create("error('initialization refused')", "invalid-init");
    BOOST_REQUIRE(!failed);
    BOOST_CHECK(failed.error().code() == srt::Error::InvalidFormat);
    BOOST_CHECK(failed.error().message().find("initialization refused") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(script_files_report_missing_and_empty_sources_and_preserve_bytes) {
    const auto root = std::filesystem::temp_directory_path() /
                      ("wolf-lua-source-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }
    } cleanup{root};
    auto missing = wolf::lua::readScript(root / "absent.lua");
    BOOST_REQUIRE(!missing);
    BOOST_CHECK(missing.error().code() == srt::Error::FileNotFound);
    const auto path = root / "source.lua";
    {
        std::ofstream empty(path, std::ios::binary);
        BOOST_REQUIRE(empty.is_open());
    }
    auto empty = wolf::lua::readScript(path);
    BOOST_REQUIRE(!empty);
    BOOST_CHECK(empty.error().code() == srt::Error::InvalidFormat);
    const std::string source = "function echo(x)\r\n    return {x}\r\nend\r\n";
    {
        std::ofstream file(path, std::ios::binary);
        file << source;
        BOOST_REQUIRE(file.good());
    }
    auto read = wolf::lua::readScript(path);
    BOOST_REQUIRE(read);
    BOOST_CHECK_EQUAL(*read, source);
    auto sandbox = wolf::lua::Sandbox::create(*read, "file-source");
    BOOST_REQUIRE(sandbox);
    auto result = (*sandbox)->callForStrings("echo", "word");
    BOOST_REQUIRE(result);
    BOOST_CHECK(*result == std::vector<std::string>({"word"}));
}
