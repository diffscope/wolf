#ifndef WOLF_TEST_SUPPORT_H
#define WOLF_TEST_SUPPORT_H

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <synthrt/Core/PackageHandle.h>
#include <synthrt/Core/SynthUnit.h>
#include <synthrt/SVS/InferenceContrib.h>
#include <synthrt/SVS/SingerContrib.h>

#include <wolf/Linguist/LinguistContrib.h>

#include <boost/test/unit_test.hpp>

/// Common setup shared by the wolf tests.
///
/// The build defines a WOLF_TEST_*_PLUGIN_DIR macro for each plugin category that a test requires
/// and none for the other categories; configure() therefore sets only the plugin paths defined for
/// this translation unit.
namespace wolf::test {

    /// Exit status that ctest interprets as a skip, so that a run without generated data is reported
    /// as not run rather than as passed. The matching SKIP_RETURN_CODE is set in CMake.
    inline constexpr int SKIP_EXIT_CODE = 77;

    /// Returns the error text of \a expected, or an empty string if \a expected holds a value.
    ///
    /// Boost.Test evaluates the message argument of BOOST_REQUIRE_MESSAGE whether or not the
    /// assertion holds, and calling error() on an Expected that holds a value is undefined
    /// behavior. The message must therefore be built through this function rather than inline.
    template <class Expected>
    std::string why(const Expected &expected) {
        return expected ? std::string() : expected.error().toString();
    }

    /// Writes \a reason to stderr and exits with the skip status.
    [[noreturn]] inline void skip(const std::string &reason) {
        std::cerr << "SKIP: " << reason << "\n";
        std::exit(SKIP_EXIT_CODE);
    }

    /// Returns the directory of the converted language packages, as specified by the
    /// WOLF_LANG_PACKAGES_SOURCE environment variable, or an empty path if the variable is unset.
    /// The packages are generated, and a checkout in which the converter has not run contains none;
    /// an empty path therefore indicates that the dependent test must be skipped.
    inline std::filesystem::path convertedRoot() {
        if (const char *override = std::getenv("WOLF_LANG_PACKAGES_SOURCE")) {
            return std::filesystem::path(override);
        }
        return std::filesystem::path();
    }

    /// Returns the directory of the generated voicebank fixture, as specified by the
    /// WOLF_VOICEBANK_FIXTURE_SOURCE environment variable, or an empty path if the variable is unset.
    inline std::filesystem::path voicebankRoot() {
        if (const char *override = std::getenv("WOLF_VOICEBANK_FIXTURE_SOURCE")) {
            return std::filesystem::path(override);
        }
        return std::filesystem::path();
    }

    /// Configures \a unit as a host does: references the library so that a linker which drops
    /// unreferenced libraries retains it, sets the package search paths to \a packagePaths, and sets
    /// one plugin search path for each category that the build defined for this test.
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
            unit.setPluginPaths(srt::InferenceCategory::NAME, paths);
        }
#endif
#ifdef WOLF_TEST_SINGER_PLUGIN_DIR
        {
            std::vector<std::filesystem::path> paths = {
                std::filesystem::path(WOLF_TEST_SINGER_PLUGIN_DIR)};
            unit.setPluginPaths(srt::SingerCategory::NAME, paths);
        }
#endif
    }

    /// Loads the package at \a directory and returns its handle, or fails the test case with the
    /// load error. The handle must outlive every use of the package because the handle keeps the
    /// package, and therefore its specs, alive.
    inline srt::PackageHandle load(srt::SynthUnit &unit, const std::filesystem::path &directory) {
        auto handle = unit.openPackage(directory, srt::SynthUnit::Load);
        if (!handle) {
            BOOST_FAIL("the package should have loaded: " + handle.error().toString());
        }
        return handle.take();
    }

}

#endif // WOLF_TEST_SUPPORT_H
