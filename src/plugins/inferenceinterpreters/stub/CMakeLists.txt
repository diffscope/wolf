project(wolfstubinference
    VERSION ${WOLF_VERSION}
    LANGUAGES CXX
)

file(GLOB_RECURSE _src *.h *.cpp)

# Test scaffolding: never installed, and built only when the tests are.
wolf_add_plugin(${PROJECT_NAME} CATEGORY ${CURRENT_PLUGIN_CATEGORY}/stub NO_EXPORT NO_INSTALL
    SOURCES ${_src}
    FEATURES cxx_std_17
    LINKS wolf
    INCLUDE_PRIVATE *
)
wolf_drop_plugin_postfix(${PROJECT_NAME})

stdc_add_plugin_metadata(TARGET ${PROJECT_NAME} IID ${CURRENT_PLUGIN_IID})

# The stub stands in for variants that have no real interpreter yet, so what it declares depends
# on what this build carries. Two providers for one triple would otherwise both be discoverable,
# and which one won would come down to directory order.
# One entry is unconditional: a variant no real interpreter claims, so it never competes for a
# triple in either build. It exists so that a fixture can ask for a module which breaks the
# provider ABI on purpose — returning a shorter batch than it was given — which three layers now
# refuse and which nothing shipped can produce.
set(WOLF_STUB_INTERPRETERS "
        {
            \"interface\": \"org.openvpi.wolf.inference.G2P\",
            \"level\": 1,
            \"variant\": \"stub-miscount\"
        }")

if(NOT WOLF_HAS_DSINFER)
    string(APPEND WOLF_STUB_INTERPRETERS ",
        {
            \"interface\": \"org.openvpi.wolf.inference.G2P\",
            \"level\": 1,
            \"variant\": \"multig2p-onnx\"
        }")
endif()

string(APPEND WOLF_STUB_INTERPRETERS "
    ")

configure_file(plugin.json.in
    "${WOLF_BUILD_PLUGINS_DIR}/${CURRENT_PLUGIN_CATEGORY}/stub/plugin.json"
    @ONLY
)
