include("conf.cmake")

wolf_init_buildsystem()

# Plugins install under lib/plugins/wolf/<category>/<name>/, the shape dsinfer uses for its own
# (lib/plugins/dsinfer/...), so that every library layered on synthrt puts its plugins under one
# root and a host deploys and searches one tree. The helper's default would be lib/wolf/plugins.
set(WOLF_BUILD_PLUGINS_DIR ${WOLF_BUILD_LIBRARY_DIR}/plugins/${WOLF_INSTALL_NAME})
set(WOLF_INSTALL_PLUGINS_DIR ${WOLF_INSTALL_LIBRARY_DIR}/plugins/${WOLF_INSTALL_NAME})

add_subdirectory(lib)

add_subdirectory(plugins)

if(WOLF_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# The helper would write a version file that accepts any newer version. wolf promises no ABI
# before 1.0, so a host built against one version must not be handed another: the CMake package
# is written here with ExactVersion. The headers are installed here too, because the helper would
# install everything under include/, and wolf/Support is not part of what a host is offered: those
# headers are the library's own tools (the task face, manifest readers, the resource cache, the
# verifier), shared with the plugins in this tree and free to change with them. What a host sees
# is Api, Linguist and Session.
wolf_install(NO_EXPORT NO_INCLUDE)

if(WOLF_INSTALL AND WOLF_DEVEL)
    install(DIRECTORY ${WOLF_INCLUDE_DIR}/
        DESTINATION ${WOLF_INSTALL_INCLUDE_DIR}
        FILES_MATCHING PATTERN "*.h" PATTERN "Support" EXCLUDE
    )
endif()

if(WOLF_INSTALL)
    qm_basic_install(
        NAME ${WOLF_INSTALL_NAME}
        VERSION ${WOLF_INSTALL_VERSION}
        COMPATIBILITY ExactVersion
        INSTALL_DIR ${WOLF_INSTALL_CMAKE_DIR}
        CONFIG_TEMPLATE ${WOLF_INSTALL_CONFIG_TEMPLATE}
        NAMESPACE ${WOLF_INSTALL_NAMESPACE}::
        EXPORT ${WOLF_EXPORT}
        WRITE_CONFIG_OPTIONS NO_CHECK_REQUIRED_COMPONENTS_MACRO
    )
endif()

wolf_finish_buildsystem()

if(WOLF_INSTALL)
    # Install copyright
    install(FILES ../LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/${WOLF_INSTALL_NAME}
    )
endif()
