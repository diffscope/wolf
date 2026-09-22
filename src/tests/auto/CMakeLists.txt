include("${SYNTHRT_MODULES_DIR}/AddAutoTest.cmake")

# One directory per layer, so that a name says what a failure is about. Support holds pure units
# that know nothing of the linguist category; Linguist the category, its declarations and its
# provider; Inference the chain and the variant interpreters; Runtime the executive tree and the
# session; EndToEnd the walks over generated packages. Every test shares TestSupport.h from here.

add_auto_test(Support/test_ManifestValues.cpp wolf)
add_auto_test(Support/test_Verifier.cpp wolf)
add_auto_test(Support/test_ResourceCache.cpp wolf)

add_auto_test(Linguist/test_LinguistContrib.cpp wolf)
add_auto_test(Linguist/test_LinguistIdentity.cpp wolf)
add_auto_test(Linguist/test_LinguistLoad.cpp wolf)
add_auto_test(Linguist/test_LinguistProvider.cpp wolf)

add_auto_test(Inference/test_OnsetRules.cpp wolf)
add_auto_test(Inference/test_S2PVariants.cpp wolf)
add_auto_test(Inference/test_PipeChain.cpp wolf)
add_auto_test(Inference/test_PinyinBackend.cpp wolf)
# Each needs the library its variants embed, so each exists only in a build that carries it.
if(WOLF_HAS_LUAJIT)
    add_auto_test(Inference/test_LuaVariants.cpp wolf)
endif()
if(WOLF_HAS_DSINFER)
    add_auto_test(Inference/test_MultiG2P.cpp wolf dsinfer::dsinfer)
endif()

add_auto_test(Runtime/test_LinguistRuntime.cpp wolf)
add_auto_test(Runtime/test_LinguistSession.cpp wolf)

add_auto_test(EndToEnd/test_ConvertedPackages.cpp wolf)
add_auto_test(EndToEnd/test_HostFlow.cpp wolf)

set(_wolf_tests
    test_ManifestValues test_Verifier test_ResourceCache
    test_LinguistContrib test_LinguistIdentity test_LinguistLoad test_LinguistProvider
    test_OnsetRules test_S2PVariants test_PipeChain test_PinyinBackend test_LuaVariants
    test_MultiG2P
    test_LinguistRuntime test_LinguistSession
    test_ConvertedPackages test_HostFlow
)

# The tests load the library under test, synthrt, its stdcorelib dependencies and, where present,
# dsinfer and ONNX Runtime at run time. Windows resolves those through PATH; the ELF and Mach-O
# loaders need their own variables, so name all three. Every test target gets the same
# environment: a test that forgot to be listed would start on Linux, where the build tree carries
# an rpath, and fail on Windows, where it does not.
#
# This covers the executables. A plugin loaded at run time resolves its own dependencies through
# the loader's search path for plugins, which on Windows is the plugin's directory, the
# executable's directory and the directories the host registered, not PATH. A deployment puts
# the libraries a plugin needs beside the executable or registers their directory.
set(_wolf_test_runtime_dirs
    "$<TARGET_FILE_DIR:wolf>"
    "$<TARGET_FILE_DIR:synthrt::synthrt>"
    "$<TARGET_FILE_DIR:stdcorelib::plugin>"
    "$<TARGET_FILE_DIR:stdcorelib::stdcorelib>"
)
if(WOLF_HAS_DSINFER)
    list(APPEND _wolf_test_runtime_dirs "$<TARGET_FILE_DIR:dsinfer::dsinfer>")
endif()
if(WOLF_ONNXRUNTIME_DIR)
    list(APPEND _wolf_test_runtime_dirs "${WOLF_ONNXRUNTIME_DIR}")
endif()
set(_wolf_test_env "")
foreach(_var IN ITEMS PATH LD_LIBRARY_PATH DYLD_LIBRARY_PATH)
    foreach(_dir IN LISTS _wolf_test_runtime_dirs)
        list(APPEND _wolf_test_env "${_var}=path_list_prepend:${_dir}")
    endforeach()
endforeach()

foreach(_test IN LISTS _wolf_tests)
    if(TARGET ${_test})
        target_include_directories(${_test} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        set_tests_properties(${_test} PROPERTIES ENVIRONMENT_MODIFICATION "${_wolf_test_env}")
    endif()
endforeach()

# Where the generated data is. Passed as environment rather than compiled in, so pointing a test
# at a different copy needs no rebuild, and a tree without the data skips instead of looking
# somewhere else. The tests that need it exit with 77 when it is absent, which ctest shows as a
# skip rather than a pass.
set(_wolf_data_env "")
if(WOLF_LANG_PACKAGES_SOURCE)
    list(APPEND _wolf_data_env "WOLF_LANG_PACKAGES_SOURCE=${WOLF_LANG_PACKAGES_SOURCE}")
endif()
if(WOLF_VOICEBANK_FIXTURE_SOURCE)
    list(APPEND _wolf_data_env "WOLF_VOICEBANK_FIXTURE_SOURCE=${WOLF_VOICEBANK_FIXTURE_SOURCE}")
endif()
foreach(_test IN ITEMS test_ConvertedPackages test_HostFlow test_MultiG2P)
    if(TARGET ${_test})
        set_tests_properties(${_test} PROPERTIES SKIP_RETURN_CODE 77)
        if(_wolf_data_env)
            set_tests_properties(${_test} PROPERTIES ENVIRONMENT "${_wolf_data_env}")
        endif()
    endif()
endforeach()

# Tests that run executives on worker threads, or deliberately stop a stage that never returns on
# its own, carry a timeout: if an interrupt or a wait ever stops working the test must fail rather
# than hang the build.
foreach(_test IN ITEMS test_PipeChain test_LuaVariants test_LinguistRuntime test_LinguistSession)
    if(TARGET ${_test})
        set_tests_properties(${_test} PROPERTIES TIMEOUT 120)
    endif()
endforeach()

# A plugin search path holds one subdirectory per plugin, so the paths handed to the tests are the
# parents of the plugin directories.
set(_wolf_fixture_dir "${CMAKE_CURRENT_SOURCE_DIR}/packages")
set(_wolf_linguist_plugins "$<TARGET_FILE_DIR:wolflinguistprovider>/..")
set(_wolf_singer_plugins "$<TARGET_FILE_DIR:wolfstubsinger>/..")

if(TARGET test_LinguistProvider)
    add_dependencies(test_LinguistProvider wolflinguistprovider)
    target_compile_definitions(test_LinguistProvider PRIVATE
        WOLF_TEST_PLUGIN_FILE="$<TARGET_FILE:wolflinguistprovider>"
        WOLF_TEST_PLUGIN_DIR="$<TARGET_FILE_DIR:wolflinguistprovider>"
    )
endif()

if(TARGET test_LinguistIdentity)
    target_compile_definitions(test_LinguistIdentity PRIVATE
        WOLF_TEST_PACKAGE_DIR="${WOLF_SOURCE_DIR}/packages"
        WOLF_TEST_FIXTURE_DIR="${_wolf_fixture_dir}"
    )
endif()

if(TARGET test_LinguistLoad)
    add_dependencies(test_LinguistLoad wolflinguistprovider wolfstubinference wolfs2p)
    target_compile_definitions(test_LinguistLoad PRIVATE
        WOLF_TEST_PACKAGE_DIR="${WOLF_SOURCE_DIR}/packages"
        WOLF_TEST_FIXTURE_DIR="${_wolf_fixture_dir}"
        WOLF_TEST_LINGUIST_PLUGIN_DIR="${_wolf_linguist_plugins}"
        WOLF_TEST_INFERENCE_PLUGIN_DIR="$<TARGET_FILE_DIR:wolfstubinference>/.."
    )
endif()

foreach(_test IN ITEMS test_LinguistRuntime test_LinguistSession)
    if(TARGET ${_test})
        add_dependencies(${_test} wolflinguistprovider wolfstubinference wolfstubsinger wolfs2p)
        target_compile_definitions(${_test} PRIVATE
            WOLF_TEST_PACKAGE_DIR="${WOLF_SOURCE_DIR}/packages"
            WOLF_TEST_FIXTURE_DIR="${_wolf_fixture_dir}"
            WOLF_TEST_LINGUIST_PLUGIN_DIR="${_wolf_linguist_plugins}"
            WOLF_TEST_INFERENCE_PLUGIN_DIR="$<TARGET_FILE_DIR:wolfstubinference>/.."
            WOLF_TEST_SINGER_PLUGIN_DIR="${_wolf_singer_plugins}"
        )
    endif()
endforeach()

if(TARGET test_ConvertedPackages)
    add_dependencies(test_ConvertedPackages wolflinguistprovider wolfstubinference wolfs2p)
    target_compile_definitions(test_ConvertedPackages PRIVATE
        WOLF_TEST_PACKAGE_DIR="${WOLF_SOURCE_DIR}/packages"
        WOLF_TEST_LINGUIST_PLUGIN_DIR="${_wolf_linguist_plugins}"
        WOLF_TEST_INFERENCE_PLUGIN_DIR="$<TARGET_FILE_DIR:wolfstubinference>/.."
    )
endif()

if(TARGET test_S2PVariants)
    add_dependencies(test_S2PVariants wolfs2p)
    target_compile_definitions(test_S2PVariants PRIVATE
        WOLF_TEST_INFERENCE_PLUGIN_DIR="$<TARGET_FILE_DIR:wolfs2p>/.."
    )
endif()

if(TARGET test_OnsetRules)
    add_dependencies(test_OnsetRules wolfonset)
    target_compile_definitions(test_OnsetRules PRIVATE
        WOLF_TEST_INFERENCE_PLUGIN_DIR="$<TARGET_FILE_DIR:wolfonset>/.."
    )
endif()

# The chain and the engine are exercised through the whole tree: a singer declares a language, the
# language declares a chain, and the chain reaches its backend through an import. Nothing below
# that is reachable from a test any other way.
foreach(_test IN ITEMS test_PipeChain test_PinyinBackend)
    if(TARGET ${_test})
        add_dependencies(${_test} wolflinguistprovider wolfchain wolfpinyin wolfs2p wolfstubsinger)
        # One case needs a package whose phoneme stage is a script, which cannot load at all
        # without the scripted variants.
        if(WOLF_HAS_LUAJIT)
            add_dependencies(${_test} wolflua)
            target_compile_definitions(${_test} PRIVATE WOLF_TEST_HAS_LUAJIT=1)
        endif()
        target_compile_definitions(${_test} PRIVATE
            WOLF_TEST_FIXTURE_DIR="${_wolf_fixture_dir}"
            WOLF_TEST_LINGUIST_PLUGIN_DIR="${_wolf_linguist_plugins}"
            WOLF_TEST_INFERENCE_PLUGIN_DIR="$<TARGET_FILE_DIR:wolfchain>/.."
            WOLF_TEST_SINGER_PLUGIN_DIR="${_wolf_singer_plugins}"
        )
    endif()
endforeach()

if(TARGET test_MultiG2P)
    add_dependencies(test_MultiG2P wolflinguistprovider wolfchain wolfmultig2p wolfs2p
                     wolfstubsinger)
    target_compile_definitions(test_MultiG2P PRIVATE
        WOLF_TEST_FIXTURE_DIR="${_wolf_fixture_dir}"
        WOLF_TEST_LINGUIST_PLUGIN_DIR="${_wolf_linguist_plugins}"
        WOLF_TEST_INFERENCE_PLUGIN_DIR="$<TARGET_FILE_DIR:wolfchain>/.."
        WOLF_TEST_SINGER_PLUGIN_DIR="${_wolf_singer_plugins}"
        WOLF_TEST_DRIVER_PLUGIN_DIR="${WOLF_DSINFER_DRIVER_DIR}"
        WOLF_TEST_ONNXRUNTIME_DIR="${WOLF_ONNXRUNTIME_DIR}"
    )
endif()

# Walks the domain the way a host does, over the converted packages plus the voicebank shaped
# fixture that closes them. Both are generated rather than committed.
if(TARGET test_HostFlow)
    add_dependencies(test_HostFlow wolflinguistprovider wolfchain wolfpinyin wolfs2p wolfonset
                     wolfstubsinger)
    target_compile_definitions(test_HostFlow PRIVATE
        WOLF_TEST_LINGUIST_PLUGIN_DIR="${_wolf_linguist_plugins}"
        WOLF_TEST_INFERENCE_PLUGIN_DIR="$<TARGET_FILE_DIR:wolfchain>/.."
        WOLF_TEST_SINGER_PLUGIN_DIR="${_wolf_singer_plugins}"
    )
endif()

if(TARGET test_LuaVariants)
    add_dependencies(test_LuaVariants wolflua)
    target_compile_definitions(test_LuaVariants PRIVATE
        WOLF_TEST_INFERENCE_PLUGIN_DIR="$<TARGET_FILE_DIR:wolflua>/.."
    )
endif()
