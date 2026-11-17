include("conf.cmake")

wolf_init_buildsystem()

# Plugins install under lib/plugins/wolf/<category>/<name>/, the layout that dsinfer uses for its
# own plugins (lib/plugins/dsinfer/...), so that every library built on synthrt installs its
# plugins under one root and a host deploys and searches a single tree. The default of the helper
# would be lib/wolf/plugins.
set(WOLF_BUILD_PLUGINS_DIR ${WOLF_BUILD_LIBRARY_DIR}/plugins/${WOLF_INSTALL_NAME})
set(WOLF_INSTALL_PLUGINS_DIR ${WOLF_INSTALL_LIBRARY_DIR}/plugins/${WOLF_INSTALL_NAME})

add_subdirectory(util)

add_subdirectory(lib)

add_subdirectory(plugins)

if(WOLF_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# The headers are installed here, because the helper would install everything under include/, and
# wolf/Support is not part of the host interface: those headers are internal tools of the library
# (the task face, manifest readers, the resource cache, the verifier), shared with the plugins in
# this tree and subject to change together with them. The host interface consists of Api, Linguist
# and Session. The symbols behind Support follow the headers: the stateless helpers are compiled
# into the static linguistutil textutil executiveutil library and are exported from no binary, and the two components that
# must be unique per process are exported under WOLF_INTERNAL_EXPORT, which carries no API or ABI
# guarantee (see lib/CMakeLists.txt and Support/SupportGlobal.h). The consumer check in
# .github/consumer verifies that the package contains no Support header.
wolf_install(NO_INCLUDE)

if(WOLF_INSTALL AND WOLF_DEVEL)
    install(DIRECTORY ${WOLF_INCLUDE_DIR}/
        DESTINATION ${WOLF_INSTALL_INCLUDE_DIR}
        FILES_MATCHING PATTERN "*.h" PATTERN "Support" EXCLUDE
    )
endif()

wolf_finish_buildsystem()

if(WOLF_INSTALL)
    # Install copyright
    install(FILES ../LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/${WOLF_INSTALL_NAME}
    )
endif()
