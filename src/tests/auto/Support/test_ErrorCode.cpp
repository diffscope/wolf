#include <system_error>

#include <synthrt/Support/Error.h>

#include <wolf/Support/ErrorCode.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(error_codes_retain_the_wolf_domain_in_framework_errors) {
    for (const auto code : {wolf::ErrorCode::NoWorker, wolf::ErrorCode::ResourceInvalid}) {
        const std::error_code standard = code;
        BOOST_CHECK(&standard.category() == &wolf::errorCategory());
        BOOST_CHECK(!standard.message().empty());

        const srt::Error framework(code, "wolf operation failed");
        BOOST_CHECK(framework.code() == standard);
    }
}
