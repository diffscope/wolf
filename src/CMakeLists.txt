include("conf.cmake")

wolf_init_buildsystem()

add_subdirectory(lib)

if(WOLF_BUILD_TESTS)
    add_subdirectory(tests)
endif()

wolf_install()

wolf_finish_buildsystem()

if(WOLF_INSTALL)
    # Install copyright
    install(FILES ../LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/${WOLF_INSTALL_NAME}
    )
endif()
