include("conf.cmake")

wolf_init_buildsystem()

# The plugin layout is the default of the repo helper, lib/<install name>/plugins, which is
# lib/synthrt-wolf/plugins here. synthrt and dsinfer use the same default, so every library built
# on synthrt keeps its plugins beside its own library directory rather than in a shared
# lib/plugins root.

add_subdirectory(util)

add_subdirectory(lib)

add_subdirectory(plugins)

if(WOLF_BUILD_TESTS)
    add_subdirectory(tests)
endif()

wolf_install()
if(COMMAND x_vcpkg_install_local_dependencies)
    x_vcpkg_install_local_dependencies(TARGETS synthrt-wolf DESTINATION bin)
endif()

# The plugins resolve their dependencies through the host process. Place the DLLs that only they
# use in bin as well, so that a host that adds bin to its search path finds every dependency
# without a copy in each plugin directory.
if(WIN32 AND _VCPKG_INSTALLED_DIR)
    foreach(_name IN ITEMS re2.dll abseil_dll.dll cpp-pinyin2.dll lua51.dll)
        install(FILES "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/${_name}"
            DESTINATION bin OPTIONAL
        )
    endforeach()
endif()

wolf_finish_buildsystem()

if(WOLF_INSTALL)
    # Install copyright
    install(FILES ../LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/${WOLF_INSTALL_NAME}
    )
endif()
