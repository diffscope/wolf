#include <system_error>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <multig2p/Decoder.h>

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

namespace {
    struct Models {
        std::filesystem::path root =
            std::filesystem::temp_directory_path() /
            ("decoder-tests-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Models() {
            std::filesystem::create_directories(root);
        }
        ~Models() {
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
        void write(const char *name, const std::string &text) {
            std::ofstream stream(root / name);
            stream << text;
            BOOST_REQUIRE(stream.good());
        }
    };
}

BOOST_AUTO_TEST_CASE(model_resolution_uses_bundle_names_and_reports_missing_files) {
    Models models;
    models.write(
        "bundle.json",
        R"({"bundle_version":1,"languages":["eng"],"files":{"encoder":"enc.onnx","decoder_step_init":"init.onnx","decoder_step":"step.onnx"}})");
    auto bundle = wolf::multig2p::Bundle::load(models.root / "bundle.json");
    BOOST_REQUIRE(bundle);
    auto missing = wolf::multig2p::Decoder::resolveModels(bundle.value(), models.root);
    BOOST_REQUIRE(!missing);
    BOOST_CHECK(missing.error().code() == srt::Error::FileNotFound);
    models.write("enc.onnx", "encoder");
    models.write("init.onnx", "initial");
    models.write("step.onnx", "step");
    auto resolved = wolf::multig2p::Decoder::resolveModels(bundle.value(), models.root);
    BOOST_REQUIRE(resolved);
    BOOST_REQUIRE_EQUAL(resolved.value().size(), 3u);
    BOOST_CHECK_EQUAL(resolved.value()[0].logical, "encoder");
    BOOST_CHECK(resolved.value()[0].path == models.root / "enc.onnx");
    BOOST_CHECK_EQUAL(resolved.value()[1].logical, "decoder_step_init");
    BOOST_CHECK_EQUAL(resolved.value()[2].logical, "decoder_step");
    BOOST_CHECK(wolf::multig2p::Decoder::verifyModels(bundle.value(), models.root));
}
