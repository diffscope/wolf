# The file name of a plugin is the name field of its plugin.json, and the bundle loader tries only
# the platform prefix and suffix around that name. The debug postfix of the library targets would
# turn wolfchain.dll into wolfchaind.dll and make every plugin undiscoverable in a Debug build, so
# the postfix is cleared for every plugin immediately after its declaration.
function(wolf_drop_plugin_postfix _target)
    set_target_properties(${_target} PROPERTIES DEBUG_POSTFIX "")
endfunction()

# Declares a plugin of this repository from the directory that contains it:
#
#   wolf_add_interpreter_plugin(<target> [NO_INSTALL] [NO_MANIFEST] [LINKS_PRIVATE <libs>...])
#
# The directory name is the name of the plugin within its category, so it is derived from the
# directory instead of being repeated: the binary is placed under <category>/<dir>, with plugin.json
# beside it, in both the build tree and the install tree. Every plugin links wolf, and links
# wolfsupport privately for the stateless helpers (see lib/CMakeLists.txt); LINKS_PRIVATE adds
# further private dependencies. NO_INSTALL is intended for test scaffolding. NO_MANIFEST leaves
# plugin.json to the caller, for a plugin whose manifest depends on the build configuration.
#
# The CMakeLists.txt of the category directory sets CURRENT_PLUGIN_CATEGORY and CURRENT_PLUGIN_IID.
function(wolf_add_interpreter_plugin _target)
    cmake_parse_arguments(FUNC "NO_INSTALL;NO_MANIFEST" "" "LINKS_PRIVATE" ${ARGN})
    get_filename_component(_name "${CMAKE_CURRENT_SOURCE_DIR}" NAME)

    file(GLOB_RECURSE _src "${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

    set(_install_option)
    if(FUNC_NO_INSTALL)
        set(_install_option NO_INSTALL)
    endif()

    wolf_add_plugin(${_target} CATEGORY ${CURRENT_PLUGIN_CATEGORY}/${_name} NO_EXPORT
        ${_install_option}
        SOURCES ${_src}
        FEATURES cxx_std_17
        LINKS wolf
        LINKS_PRIVATE wolfsupport ${FUNC_LINKS_PRIVATE}
        INCLUDE_PRIVATE *
    )
    wolf_drop_plugin_postfix(${_target})

    stdc_add_plugin_metadata(TARGET ${_target} IID ${CURRENT_PLUGIN_IID})

    if(FUNC_NO_MANIFEST)
        return()
    endif()

    # configure_file is used instead of a copy command because it re-runs when plugin.json changes
    # and restores the output if it is deleted; the copy command does neither. A stale manifest
    # causes failures, because discovery resolves the library through its name field.
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/plugin.json"
        "${WOLF_BUILD_PLUGINS_DIR}/${CURRENT_PLUGIN_CATEGORY}/${_name}/plugin.json"
        COPYONLY
    )

    if(WOLF_INSTALL AND NOT FUNC_NO_INSTALL)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/plugin.json"
            DESTINATION "${WOLF_INSTALL_PLUGINS_DIR}/${CURRENT_PLUGIN_CATEGORY}/${_name}"
        )
    endif()
endfunction()

add_subdirectory(linguistproviders)

add_subdirectory(inferenceinterpreters)

# The stub singer provider exists only so that the linguist domain can be tested without a
# voicebank.
if(WOLF_BUILD_TESTS)
    add_subdirectory(singerproviders)
endif()
