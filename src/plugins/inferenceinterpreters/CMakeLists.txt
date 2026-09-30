get_filename_component(_dir_name ${CMAKE_CURRENT_LIST_DIR} NAME)
set(CURRENT_PLUGIN_CATEGORY ${_dir_name})

# The inference category belongs to synthrt, so its interpreters carry synthrt's plugin IID rather
# than one of wolf's. See docs/linguist-inference-contract.md section 1.
set(CURRENT_PLUGIN_IID org.openvpi.synthrt.plugin.InferenceInterpreter)

# Test scaffolding, built only with the tests. See src/plugins/CMakeLists.txt.
if(WOLF_BUILD_TESTS)
    add_subdirectory(stub)
endif()
