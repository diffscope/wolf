# Every plugin is declared by wolf_add_interpreter_plugin(), which combines wolf_add_plugin() with
# the building blocks of AddPlugin.cmake in the synthrt package: the sources but main.cpp are
# compiled into the object library <target>_objects that the tests link. AddPlugin.cmake
# describes the layout and the include namespace.
include("${SYNTHRT_MODULES_DIR}/AddPlugin.cmake")

# Declares a plugin of this repository from the directory that contains it:
#
#   wolf_add_interpreter_plugin(<target> [LINKS <libs>...] [LINKS_PRIVATE <libs>...])
#
# The directory name is the name of the plugin within its category and its include namespace: the
# binary is placed under <category>/<dir>, with plugin.json beside it, in both the build tree and
# the install tree. Every plugin links wolf. LINKS adds the dependencies that the headers of the
# plugin use, and LINKS_PRIVATE those that only its sources use, such as the static libraries under
# src/util that hold the stateless helpers (see lib/CMakeLists.txt).
#
# The CMakeLists.txt of the category directory sets CURRENT_PLUGIN_CATEGORY and CURRENT_PLUGIN_IID.
function(wolf_add_interpreter_plugin _target)
    cmake_parse_arguments(FUNC "" "" "LINKS;LINKS_PRIVATE" ${ARGN})
    get_filename_component(_name "${CMAKE_CURRENT_SOURCE_DIR}" NAME)

    synthrt_add_plugin_objects(${_target}
        FEATURES cxx_std_17
        LINKS synthrt-wolf ${FUNC_LINKS}
        LINKS_PRIVATE ${FUNC_LINKS_PRIVATE}
        INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/..
        INCLUDE_PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/*
    )
    wolf_add_plugin(${_target} CATEGORY ${CURRENT_PLUGIN_CATEGORY}/${_name}
        SOURCES main.cpp
        FEATURES cxx_std_17
        LINKS_PRIVATE ${_target}_objects ${FUNC_LINKS_PRIVATE}
    )
    stdc_add_plugin_metadata(TARGET ${_target} IID ${CURRENT_PLUGIN_IID})
    synthrt_configure_plugin_manifest(${_target} wolf)
endfunction()

add_subdirectory(linguistproviders)

add_subdirectory(inferenceinterpreters)
