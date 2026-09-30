include("${SYNTHRT_MODULES_DIR}/AddAutoTest.cmake")

# One directory per layer, so that a name says what a failure is about. Support holds pure units
# that know nothing of the linguist category; Linguist the category, its declarations and its
# provider; Inference the chain and the variant interpreters; Runtime the executive tree and the
# session; EndToEnd the walks over generated packages. Every test shares TestSupport.h from here.

# Registers one test and records its target, so that the settings every test needs below are
# applied to exactly the tests that exist. A hand kept second list of names was the alternative,
# and a test missing from it silently lost its environment.
set(_wolf_tests "")
macro(wolf_add_test _source)
    add_auto_test(${_source} ${ARGN})
    get_filename_component(_wolf_test_name ${_source} NAME_WE)
    list(APPEND _wolf_tests ${_wolf_test_name})
endmacro()

# The stateless helpers are not exported from libwolf; their tests link the static library that
# carries them, as the plugins do.
wolf_add_test(Support/test_ManifestValues.cpp wolf wolfsupport)
wolf_add_test(Support/test_Verifier.cpp wolf wolfsupport)
wolf_add_test(Support/test_ResourceCache.cpp wolf)
if(TARGET test_ResourceCache)
    # The test reads the cache's private state through the probe ResourceCache befriends.
    target_include_directories(test_ResourceCache PRIVATE "${WOLF_SOURCE_DIR}/src/lib/Support")
endif()

wolf_add_test(Linguist/test_LinguistContrib.cpp wolf)
wolf_add_test(Linguist/test_LinguistProvider.cpp wolf)

# The tests load the library under test, synthrt and its stdcorelib dependencies at run time.
# Windows resolves those through PATH; the ELF and Mach-O loaders need their own variables, so
# name all three. Every test target gets the same
# environment: a test that forgot to be listed would start on Linux, where the build tree carries
# an rpath, and fail on Windows, where it does not; wolf_add_test() is what keeps every test on
# the list.
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

if(TARGET test_LinguistProvider)
    add_dependencies(test_LinguistProvider wolflinguistprovider)
    target_compile_definitions(test_LinguistProvider PRIVATE
        WOLF_TEST_PLUGIN_FILE="$<TARGET_FILE:wolflinguistprovider>"
        WOLF_TEST_PLUGIN_DIR="$<TARGET_FILE_DIR:wolflinguistprovider>"
    )
endif()
