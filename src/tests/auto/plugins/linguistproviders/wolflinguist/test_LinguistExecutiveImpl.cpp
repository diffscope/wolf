#include <wolflinguist/LinguistExecutiveImpl.h>

#define BOOST_TEST_MAIN
#include "TestSupport.h"

BOOST_AUTO_TEST_CASE(executive_uses_bound_language_and_converts_through_owned_children) {
    LoadedTree tree;
    wolf::LinguistExecutiveImpl executive(tree.linguist());
    BOOST_CHECK_EQUAL(executive.binding().language, "zxx");
    BOOST_CHECK(!executive.g2pContribution().contributionId().empty());
    wolf::Api::Linguist::L1::ConvertInput input;
    input.depth = wolf::Api::Linguist::L1::Depth::Pronunciation;
    input.words.push_back({"word", std::nullopt, std::nullopt});
    auto result = executive.start(input);
    BOOST_REQUIRE(result);
    BOOST_REQUIRE_EQUAL(result.value()->words.size(), 1u);
    BOOST_CHECK(!result.value()->words[0].pronunciation.empty());
    BOOST_CHECK(executive.state() == srt::ITask::Succeeded);
    BOOST_REQUIRE(executive.waitForFinished());
}
