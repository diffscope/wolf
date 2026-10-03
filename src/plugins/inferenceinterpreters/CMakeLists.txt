get_filename_component(_dir_name ${CMAKE_CURRENT_LIST_DIR} NAME)
set(CURRENT_PLUGIN_CATEGORY ${_dir_name})

# The inference category belongs to synthrt, so its interpreters carry the synthrt plugin IID
# instead of a wolf IID. See docs/linguist-inference-contract.md section 1.
set(CURRENT_PLUGIN_IID org.openvpi.synthrt.plugin.InferenceInterpreter)

add_subdirectory(chain)
# The model-backed variant requires the driver framework of dsinfer, which the onnx feature
# provides. Without dsinfer the variant is not built, in the same way that synthrt omits its own
# ONNX driver.
if(WOLF_HAS_DSINFER)
    add_subdirectory(multig2p)
endif()

# The scripted variants embed LuaJIT and are built only if LuaJIT is available.
if(WOLF_HAS_LUAJIT)
    add_subdirectory(lua)
endif()

add_subdirectory(onset)
add_subdirectory(pinyin)
add_subdirectory(s2p)

# Test scaffolding, built only with the tests. See src/plugins/CMakeLists.txt.
if(WOLF_BUILD_TESTS)
    add_subdirectory(stub)
endif()
