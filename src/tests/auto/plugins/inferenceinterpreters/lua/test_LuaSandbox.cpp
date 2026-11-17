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
