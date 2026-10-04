include("${SYNTHRT_MODULES_DIR}/AddAutoTest.cmake")

# One directory per layer, so that the path of a test identifies the layer of a failure. Support
# contains pure units independent of the linguist category; Linguist the category, its declarations
# and its provider; Inference the chain and the variant interpreters; Runtime the executive tree
# and the session; EndToEnd the walks over generated packages. Every test uses TestSupport.h from
# this directory.

# Registers a test and records its target, so that the common settings below apply to exactly the
# tests that exist. The alternative was a manually maintained second list of names, and a test
# missing from that list silently lost its environment.
set(_wolf_tests "")
macro(wolf_add_test _source)
    add_auto_test(${_source} ${ARGN})
    get_filename_component(_wolf_test_name ${_source} NAME_WE)
    list(APPEND _wolf_tests ${_wolf_test_name})
endmacro()

# The stateless helpers are not exported from libwolf; their tests link the static library that
# contains them, as the plugins do.
wolf_add_test(Support/test_ManifestValues.cpp wolf wolfsupport)
wolf_add_test(Support/test_Utf8.cpp wolf wolfsupport)
wolf_add_test(Support/test_Verifier.cpp wolf wolfsupport)
wolf_add_test(Support/test_ExecutiveTask.cpp wolf wolfsupport)
wolf_add_test(Support/test_ResourceCache.cpp wolf)
if(TARGET test_ResourceCache)
    # The test reads the private state of the cache through the probe class that ResourceCache
    # declares as a friend.
    target_include_directories(test_ResourceCache PRIVATE "${WOLF_SOURCE_DIR}/src/lib/Support")
endif()

wolf_add_test(Linguist/test_LinguistContrib.cpp wolf)
wolf_add_test(Linguist/test_LinguistIdentity.cpp wolf)
wolf_add_test(Linguist/test_LinguistLoad.cpp wolf)
wolf_add_test(Linguist/test_LinguistProvider.cpp wolf)

wolf_add_test(Inference/test_OnsetRules.cpp wolf)
wolf_add_test(Inference/test_S2PVariants.cpp wolf)
wolf_add_test(Inference/test_PipeChain.cpp wolf)
wolf_add_test(Inference/test_PinyinBackend.cpp wolf)
# Each of these tests requires the library that its variants embed and is built only if that
# library is available.
if(WOLF_HAS_LUAJIT)
    wolf_add_test(Inference/test_LuaVariants.cpp wolf)
endif()
if(WOLF_HAS_DSINFER)
    wolf_add_test(Inference/test_MultiG2P.cpp wolf dsinfer::dsinfer)
endif()

wolf_add_test(Runtime/test_LinguistRuntime.cpp wolf)
wolf_add_test(Runtime/test_LinguistSession.cpp wolf)

wolf_add_test(EndToEnd/test_ConvertedPackages.cpp wolf)
wolf_add_test(EndToEnd/test_HostFlow.cpp wolf)

# The tests load the library under test, synthrt, its stdcorelib dependencies and, if present,
# dsinfer and ONNX Runtime at run time. Windows resolves these libraries through PATH; the ELF and
# Mach-O loaders use their own variables, so all three variables are set. Every test target
# receives the same environment: an unlisted test would start on Linux, where the build tree
# carries an rpath, and fail on Windows, where it does not; wolf_add_test() ensures that every test
# is listed.
#
# This covers the executables. A plugin loaded at run time resolves its own dependencies through
# the plugin search path of the loader, which on Windows consists of the plugin directory, the
# executable directory and the directories that the host registered, but not PATH. A deployment
# places the libraries that a plugin requires beside the executable or registers their directory.
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

# The location of the generated data. It is passed through the environment rather than compiled
# in, so that pointing a test at a different copy requires no rebuild, and a tree without the data
# skips the tests instead of searching elsewhere. The tests that require the data exit with 77 if
# it is absent, which ctest reports as a skip rather than a pass.
set(_wolf_data_env "")
if(WOLF_LANG_PACKAGES_SOURCE)
    list(APPEND _wolf_data_env "WOLF_LANG_PACKAGES_SOURCE=${WOLF_LANG_PACKAGES_SOURCE}")
endif()
if(WOLF_VOICEBANK_FIXTURE_SOURCE)
    list(APPEND _wolf_data_env "WOLF_VOICEBANK_FIXTURE_SOURCE=${WOLF_VOICEBANK_FIXTURE_SOURCE}")
endif()
if(WOLF_TEST_FIXTURES_SOURCE)
    list(APPEND _wolf_data_env "WOLF_TEST_FIXTURES_SOURCE=${WOLF_TEST_FIXTURES_SOURCE}")
endif()
foreach(_test IN ITEMS test_ConvertedPackages test_HostFlow test_MultiG2P
                       test_LinguistIdentity test_LinguistLoad test_PipeChain test_PinyinBackend
                       test_LinguistRuntime test_LinguistSession)
    if(TARGET ${_test})
        set_tests_properties(${_test} PROPERTIES SKIP_RETURN_CODE 77)
        if(_wolf_data_env)
            set_tests_properties(${_test} PROPERTIES ENVIRONMENT "${_wolf_data_env}")
        endif()
    endif()
endforeach()

# Tests that run executives on worker threads, or intentionally stop a stage that does not return
# by itself, have a timeout: if an interrupt or a wait stops working, the test must fail instead of
# hanging the build.
foreach(_test IN ITEMS test_PipeChain test_LuaVariants test_LinguistRuntime test_LinguistSession)
    if(TARGET ${_test})
        set_tests_properties(${_test} PROPERTIES TIMEOUT 120)
    endif()
endforeach()

# A plugin search path contains one subdirectory per plugin, so the paths passed to the tests are
# the parents of the plugin directories.
set(_wolf_linguist_plugins "$<TARGET_FILE_DIR:wolflinguistprovider>/..")
set(_wolf_singer_plugins "$<TARGET_FILE_DIR:wolfstubsinger>/..")

if(TARGET test_LinguistProvider)
    add_dependencies(test_LinguistProvider wolflinguistprovider)
    target_compile_definitions(test_LinguistProvider PRIVATE
        WOLF_TEST_PLUGIN_FILE="$<TARGET_FILE:wolflinguistprovider>"
        WOLF_TEST_PLUGIN_DIR="$<TARGET_FILE_DIR:wolflinguistprovider>"
    )
endif()

if(TARGET test_LinguistLoad)
    add_dependencies(test_LinguistLoad wolflinguistprovider wolfstubinference wolfs2p wolfchain
                     wolfpinyin wolfonset wolfstubsinger)
    if(WOLF_HAS_LUAJIT)
        add_dependencies(test_LinguistLoad wolflua)
        target_compile_definitions(test_LinguistLoad PRIVATE WOLF_TEST_HAS_LUAJIT=1)
    endif()
    if(WOLF_HAS_DSINFER)
        add_dependencies(test_LinguistLoad wolfmultig2p)
    endif()
    target_compile_definitions(test_LinguistLoad PRIVATE
        WOLF_TEST_LINGUIST_PLUGIN_DIR="${_wolf_linguist_plugins}"
        WOLF_TEST_INFERENCE_PLUGIN_DIR="$<TARGET_FILE_DIR:wolfstubinference>/.."
        WOLF_TEST_SINGER_PLUGIN_DIR="${_wolf_singer_plugins}"
        WOLF_TEST_VERDICTS_FILE="${CMAKE_CURRENT_SOURCE_DIR}/loader-verdicts.json"
    )
endif()

foreach(_test IN ITEMS test_LinguistRuntime test_LinguistSession)
    if(TARGET ${_test})
        add_dependencies(${_test} wolflinguistprovider wolfstubinference wolfstubsinger wolfs2p)
        target_compile_definitions(${_test} PRIVATE
            WOLF_TEST_LINGUIST_PLUGIN_DIR="${_wolf_linguist_plugins}"
            WOLF_TEST_INFERENCE_PLUGIN_DIR="$<TARGET_FILE_DIR:wolfstubinference>/.."
            WOLF_TEST_SINGER_PLUGIN_DIR="${_wolf_singer_plugins}"
        )
    endif()
endforeach()

if(TARGET test_ConvertedPackages)
    add_dependencies(test_ConvertedPackages wolflinguistprovider wolfstubinference wolfs2p)
    target_compile_definitions(test_ConvertedPackages PRIVATE
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

# The chain and the engine are exercised through the complete tree: a singer declares a language,
# the language declares a chain, and the chain reaches its backend through an import. The
# components below that level are not reachable from a test in any other way.
foreach(_test IN ITEMS test_PipeChain test_PinyinBackend)
    if(TARGET ${_test})
        add_dependencies(${_test} wolflinguistprovider wolfchain wolfpinyin wolfs2p wolfstubsinger)
        # One case requires a package whose phoneme stage is a script, which cannot load without
        # the scripted variants.
        if(WOLF_HAS_LUAJIT)
            add_dependencies(${_test} wolflua)
            target_compile_definitions(${_test} PRIVATE WOLF_TEST_HAS_LUAJIT=1)
        endif()
        target_compile_definitions(${_test} PRIVATE
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
        WOLF_TEST_LINGUIST_PLUGIN_DIR="${_wolf_linguist_plugins}"
        WOLF_TEST_INFERENCE_PLUGIN_DIR="$<TARGET_FILE_DIR:wolfchain>/.."
        WOLF_TEST_SINGER_PLUGIN_DIR="${_wolf_singer_plugins}"
        WOLF_TEST_DRIVER_PLUGIN_DIR="${WOLF_DSINFER_DRIVER_DIR}"
        WOLF_TEST_ONNXRUNTIME_DIR="${WOLF_ONNXRUNTIME_DIR}"
    )
endif()

# Walks the domain as a host does, over the converted packages and the voicebank-shaped fixture
# that completes them. Both are generated rather than committed.
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
