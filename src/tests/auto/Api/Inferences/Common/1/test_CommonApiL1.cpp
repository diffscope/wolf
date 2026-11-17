#include <wolf/Api/Inferences/Common/1/CommonApiL1.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(language_scheme_identity_includes_both_parts) {
    using wolf::Api::Common::L1::LanguageScheme;
    const LanguageScheme first("eng", "ipa"), same("eng", "ipa"), language("cmn", "ipa"),
        scheme("eng", "arpabet");
    BOOST_CHECK(first == same);
    BOOST_CHECK(first != language);
    BOOST_CHECK(first != scheme);
    BOOST_CHECK(LanguageScheme().empty());
    BOOST_CHECK(!LanguageScheme("eng", "").empty());
}
