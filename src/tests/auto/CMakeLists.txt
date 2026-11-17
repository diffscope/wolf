include("${SYNTHRT_MODULES_DIR}/AddAutoTest.cmake")

# One directory per layer, so that the path of a test identifies the layer of a failure. util
# contains the libraries under src/util, one directory per library, and Support the public support
# components, both independent of the linguist category; Linguist the category, its declarations and
# its provider; Runtime the executive tree and the session; EndToEnd the walks over generated
# packages; plugins the classes of single plugins, one directory per plugin. Every test uses
# TestSupport.h from this directory.

# Registers a test and records its target, so that the common settings below apply to exactly the
# tests that exist. The alternative was a manually maintained second list of names, and a test
# missing from that list silently lost its environment.
set(_wolf_tests "")
macro(wolf_add_test _source)
    add_auto_test(${_source} ${ARGN})
    get_filename_component(_wolf_test_name ${_source} NAME_WE)
    list(APPEND _wolf_tests ${_wolf_test_name})
endmacro()

# The stateless helpers are not exported from libwolf. Their tests link the static library that
# contains them, as the plugins do.
wolf_add_test(util/linguistutil/test_ManifestValues.cpp synthrt-wolf linguistutil)
wolf_add_test(util/linguistutil/test_Verifier.cpp synthrt-wolf linguistutil)
wolf_add_test(util/textutil/test_Utf8.cpp synthrt-wolf textutil)
wolf_add_test(util/executiveutil/test_ExecutiveTask.cpp synthrt-wolf executiveutil)
wolf_add_test(Support/test_ResourceCache.cpp synthrt-wolf)
if(TARGET test_ResourceCache)
    # The test reads the private state of the cache through the probe class that ResourceCache
    # declares as a friend.
    target_include_directories(test_ResourceCache PRIVATE "${WOLF_SOURCE_DIR}/src/lib/Support")
endif()

wolf_add_test(Linguist/test_LinguistContrib.cpp synthrt-wolf)
wolf_add_test(Linguist/test_LinguistIdentity.cpp synthrt-wolf)
wolf_add_test(Linguist/test_LinguistLoad.cpp synthrt-wolf)
wolf_add_test(Linguist/test_LinguistProvider.cpp synthrt-wolf)

wolf_add_test(Runtime/test_LinguistRuntime.cpp synthrt-wolf)
wolf_add_test(Runtime/test_LinguistSession.cpp synthrt-wolf)

wolf_add_test(EndToEnd/test_ConvertedPackages.cpp synthrt-wolf)
wolf_add_test(EndToEnd/test_HostFlow.cpp synthrt-wolf)

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
    "$<TARGET_FILE_DIR:synthrt-wolf>"
    "$<TARGET_FILE_DIR:synthrt::synthrt>"
    "$<TARGET_FILE_DIR:stdcorelib::plugin>"
    "$<TARGET_FILE_DIR:stdcorelib::stdcorelib>"
)
if(WOLF_HAS_DSINFER)
    list(APPEND _wolf_test_runtime_dirs "$<TARGET_FILE_DIR:synthrt::dsinfer>")
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
foreach(_test IN ITEMS test_ConvertedPackages test_HostFlow test_LinguistIdentity
                       test_LinguistLoad test_LinguistRuntime test_LinguistSession)
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
foreach(_test IN ITEMS test_LinguistRuntime test_LinguistSession)
    if(TARGET ${_test})
        set_tests_properties(${_test} PROPERTIES TIMEOUT 120)
    endif()
endforeach()

# A plugin search path contains one subdirectory per plugin, so the paths passed to the tests are
# the parents of the plugin directories.
set(_wolf_linguist_plugins "$<TARGET_FILE_DIR:wolflinguistprovider>/..")
set(_wolf_inference_plugins "$<TARGET_FILE_DIR:wolfs2p>/..")


add_subdirectory(plugins)

if(TARGET test_LinguistProvider)
    add_dependencies(test_LinguistProvider wolflinguistprovider)
    target_compile_definitions(test_LinguistProvider PRIVATE
        WOLF_TEST_PLUGIN_FILE="$<TARGET_FILE:wolflinguistprovider>"
        WOLF_TEST_PLUGIN_DIR="$<TARGET_FILE_DIR:wolflinguistprovider>"
    )
endif()

if(TARGET test_LinguistLoad)
    target_link_libraries(test_LinguistLoad PRIVATE wolfstubinference wolfstubsinger)
    add_dependencies(test_LinguistLoad wolflinguistprovider wolfs2p wolfchain wolfpinyin wolfonset)
    if(WOLF_HAS_LUAJIT)
        add_dependencies(test_LinguistLoad wolflua)
        target_compile_definitions(test_LinguistLoad PRIVATE WOLF_TEST_HAS_LUAJIT=1)
    endif()
    if(WOLF_HAS_DSINFER)
        add_dependencies(test_LinguistLoad wolfmultig2p)
    endif()
    target_compile_definitions(test_LinguistLoad PRIVATE
        WOLF_TEST_LINGUIST_PLUGIN_DIR="${_wolf_linguist_plugins}"
        WOLF_TEST_INFERENCE_PLUGIN_DIR="${_wolf_inference_plugins}"
        WOLF_TEST_VERDICTS_FILE="${CMAKE_CURRENT_SOURCE_DIR}/loader-verdicts.json"
    )
endif()

foreach(_test IN ITEMS test_LinguistRuntime test_LinguistSession)
    if(TARGET ${_test})
        target_link_libraries(${_test} PRIVATE wolfstubinference wolfstubsinger)
        add_dependencies(${_test} wolflinguistprovider wolfs2p)
        target_compile_definitions(${_test} PRIVATE
            WOLF_TEST_LINGUIST_PLUGIN_DIR="${_wolf_linguist_plugins}"
            WOLF_TEST_INFERENCE_PLUGIN_DIR="${_wolf_inference_plugins}"
        )
    endif()
endforeach()

if(TARGET test_ConvertedPackages)
    target_link_libraries(test_ConvertedPackages PRIVATE wolfstubinference wolfstubsinger)
    add_dependencies(test_ConvertedPackages wolflinguistprovider wolfs2p)
    target_compile_definitions(test_ConvertedPackages PRIVATE
        WOLF_TEST_LINGUIST_PLUGIN_DIR="${_wolf_linguist_plugins}"
        WOLF_TEST_INFERENCE_PLUGIN_DIR="${_wolf_inference_plugins}"
    )
endif()


# Walks the domain as a host does, over the converted packages and the voicebank-shaped fixture
# that completes them. Both are generated rather than committed.
if(TARGET test_HostFlow)
    target_link_libraries(test_HostFlow PRIVATE wolfstubinference wolfstubsinger)
    add_dependencies(test_HostFlow wolflinguistprovider wolfchain wolfpinyin wolfs2p wolfonset)
    target_compile_definitions(test_HostFlow PRIVATE
        WOLF_TEST_LINGUIST_PLUGIN_DIR="${_wolf_linguist_plugins}"
        WOLF_TEST_INFERENCE_PLUGIN_DIR="${_wolf_inference_plugins}"
    )
endif()
