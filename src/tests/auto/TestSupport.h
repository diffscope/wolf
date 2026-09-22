#ifndef WOLF_TEST_SUPPORT_H
#define WOLF_TEST_SUPPORT_H

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Core/SynthUnit.h>

#include <wolf/Linguist/LinguistContrib.h>

#include <boost/test/unit_test.hpp>

/// What every wolf test does before it does anything else, written once.
///
/// The build defines a WOLF_TEST_*_PLUGIN_DIR for each plugin category a test needs, and nothing
/// for the ones it does not, so configure() sets whichever paths this translation unit was given.
namespace wolf::test {

    /// Exit status ctest reads as a skip, so a run without generated data is reported as not run
    /// rather than passed off as success. The matching SKIP_RETURN_CODE is set in CMake.
    inline constexpr int SKIP_EXIT_CODE = 77;

    /// Returns why \a expected failed, or an empty string when it did not.
    ///
    /// Boost.Test evaluates the message argument of BOOST_REQUIRE_MESSAGE whether or not the
    /// assertion holds, and error() on an Expected that holds a value is undefined, so the
    /// message has to be built through this rather than written inline.
    template <class Expected>
    std::string why(const Expected &expected) {
        return expected ? std::string() : expected.error().toString();
    }

    /// Leaves with the skip status after saying why on stderr.
    [[noreturn]] inline void skip(const std::string &reason) {
        std::cerr << "SKIP: " << reason << "\n";
        std::exit(SKIP_EXIT_CODE);
    }

    /// Where the converted language packages are, as the build was told through the environment.
    /// Empty when it was not told: the packages are generated, and a checkout that never ran the
    /// converter has none, which is a reason to skip and not a place to look.
    inline std::filesystem::path convertedRoot() {
        if (const char *override = std::getenv("WOLF_LANG_PACKAGES_SOURCE")) {
            return std::filesystem::path(override);
        }
        return std::filesystem::path();
    }

    /// Where the generated voicebank fixture is, on the same terms as convertedRoot().
    inline std::filesystem::path voicebankRoot() {
        if (const char *override = std::getenv("WOLF_VOICEBANK_FIXTURE_SOURCE")) {
            return std::filesystem::path(override);
        }
        return std::filesystem::path();
    }

    /// Wires \a unit the way a host does: names the library so a linker that drops unreferenced
    /// libraries kept it, sets the package search paths given, and sets one plugin search path per
    /// category the build defined for this test.
    inline void configure(srt::SynthUnit &unit, std::vector<std::filesystem::path> packagePaths) {
        wolf::linkLinguistCategory();
        unit.setPackagePaths(packagePaths);
#ifdef WOLF_TEST_LINGUIST_PLUGIN_DIR
        {
            std::vector<std::filesystem::path> paths = {
                std::filesystem::path(WOLF_TEST_LINGUIST_PLUGIN_DIR)};
            unit.setPluginPaths(wolf::LINGUIST_CATEGORY, paths);
        }
#endif
#ifdef WOLF_TEST_INFERENCE_PLUGIN_DIR
        {
            std::vector<std::filesystem::path> paths = {
                std::filesystem::path(WOLF_TEST_INFERENCE_PLUGIN_DIR)};
            unit.setPluginPaths("inference", paths);
        }
#endif
#ifdef WOLF_TEST_SINGER_PLUGIN_DIR
        {
            std::vector<std::filesystem::path> paths = {
                std::filesystem::path(WOLF_TEST_SINGER_PLUGIN_DIR)};
            unit.setPluginPaths("singer", paths);
        }
#endif
    }

    /// Loads the package at \a directory, or fails the case saying why. The handle has to outlive
    /// every use: it is what keeps the package, and therefore its specs, alive.
    inline srt::PackageHandle load(srt::SynthUnit &unit, const std::filesystem::path &directory) {
        auto handle = unit.openPackage(directory, srt::SynthUnit::Load);
        if (!handle) {
            BOOST_FAIL("the package should have loaded: " + handle.error().toString());
        }
        return handle.take();
    }

}

#endif // WOLF_TEST_SUPPORT_H
