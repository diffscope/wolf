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

    /// Where the conversion writes. Absent unless someone has run it, since converted resources
    /// are never committed.
    ///
    /// An environment override lets the same check run against an unpacked release, which is what
    /// actually ships: verifying the directories the converter wrote does not prove the archives
    /// built from them are sound.
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
/// This is the gate before anything is published: a package that the loader rejects is a
/// conversion defect, and the only way to know is to make the loader read it. Cross-package
/// dependencies are covered too, since the chain packages import the shared backend.
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
        // A fresh unit per package, so one package's committed state cannot mask another's
        // failure.
        srt::SynthUnit unit;
        wolf::test::configure(unit, searchPaths);

        auto handle = unit.openPackage(directory, srt::SynthUnit::Load);
        if (!handle) {
            failures.push_back(directory.filename().string() + ": " + handle.error().toString());
            continue;
        }
        if (handle->contributions("inference").empty() &&
            handle->contributions(wolf::LINGUIST_CATEGORY).empty()) {
            failures.push_back(directory.filename().string() + ": carries no contributions");
        }
    }

    for (const auto &failure : failures) {
        BOOST_ERROR(failure);
    }
    BOOST_TEST_MESSAGE("verified " << directories.size() << " converted packages");
}

BOOST_AUTO_TEST_SUITE_END()
