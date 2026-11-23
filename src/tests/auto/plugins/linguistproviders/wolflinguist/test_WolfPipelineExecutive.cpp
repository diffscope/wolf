#include <wolflinguist/WolfPipelineExecutive.h>

#define BOOST_TEST_MAIN
#include "TestSupport.h"

BOOST_AUTO_TEST_CASE(pipeline_routes_languages_and_supervises_created_linguists) {
    LoadedTree tree;
    auto languages = wolf::SingerLanguages::read(tree.singer());
    BOOST_REQUIRE(languages);
    wolf::WolfPipelineExecutive pipeline(tree.singer(), languages.take());
    BOOST_CHECK(pipeline.languages() == std::vector<std::string>({"zxx"}));
    wolf::Api::Linguist::L1::RuntimeOptions options;
    auto linguist = pipeline.createLinguist("zxx", options);
    BOOST_REQUIRE(linguist);
    BOOST_CHECK(linguist.value()->parent() == &pipeline);
    BOOST_CHECK_EQUAL(pipeline.children().size(), 1u);
    auto missing = pipeline.createLinguist("cmn", options);
    BOOST_REQUIRE(!missing);
    delete linguist.value();
    BOOST_CHECK(pipeline.children().empty());
}
