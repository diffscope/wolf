#include <filesystem>
#include <string>
#include <vector>

#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Core/SynthUnit.h>

#include <wolf/Linguist/LinguistContrib.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

#include "TestSupport.h"

namespace fs = std::filesystem;

namespace {

    /// Returns the conversion output directory specified by WOLF_LANG_PACKAGES_SOURCE. Converted
    /// resources are never committed, so the directory exists only if the converter has run.
    ///
    /// Setting the variable to an unpacked release runs the same check against the shipped
    /// archives. Verifying the directories written by the converter does not prove that the
    /// archives built from them are sound.
    using wolf::test::convertedRoot;

    struct DataOrSkip {
        DataOrSkip() {
            if (!fs::is_directory(convertedRoot())) {
                wolf::test::skip("no converted packages; set WOLF_LANG_PACKAGES_SOURCE or run "
                                 "scripts/convert-g2p-packages.py");
            }
        }
    };

}

BOOST_TEST_GLOBAL_FIXTURE(DataOrSkip);

BOOST_AUTO_TEST_SUITE(test_ConvertedPackages)

/// Loads every converted package with the real loader.
///
/// This test is the gate before publication: a package that the loader rejects indicates a
/// conversion defect, and only the loader can detect such a defect. Cross-package dependencies are
/// also covered because the chain packages import the shared backend.
BOOST_AUTO_TEST_CASE(test_ConvertedPackages_AllLoad) {
    const auto root = convertedRoot();

    std::vector<fs::path> searchPaths = {root, fs::path(WOLF_TEST_PACKAGE_DIR)};
    std::vector<fs::path> directories;
    for (const auto &entry : fs::directory_iterator(root)) {
        if (entry.is_directory() && fs::exists(entry.path() / "desc.json")) {
            directories.push_back(entry.path());
        }
    }
    BOOST_REQUIRE_MESSAGE(!directories.empty(), "the conversion produced no packages");

    std::vector<std::string> failures;
    for (const auto &directory : directories) {
        // A fresh unit per package ensures that the committed state of one package cannot mask the
        // failure of another package.
        srt::SynthUnit unit;
        wolf::test::configure(unit, searchPaths);

        auto handle = unit.openPackage(directory, srt::SynthUnit::Load);
        if (!handle) {
            failures.push_back(directory.filename().string() + ": " + handle.error().toString());
            continue;
        }
        if (handle->contributions("inference").empty() &&
            handle->contributions(wolf::LINGUIST_CATEGORY).empty()) {
            failures.push_back(directory.filename().string() + ": declares no contributions");
        }
    }

    for (const auto &failure : failures) {
        BOOST_ERROR(failure);
    }
    BOOST_TEST_MESSAGE("verified " << directories.size() << " converted packages");
}

BOOST_AUTO_TEST_SUITE_END()
