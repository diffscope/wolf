project(language
    VERSION ${WOLF_VERSION}
    LANGUAGES CXX
)

file(GLOB_RECURSE _src *.h *.cpp)

wolf_add_plugin(${PROJECT_NAME} CATEGORY ${CURRENT_PLUGIN_CATEGORY}/language NO_EXPORT
    SOURCES ${_src}
    FEATURES cxx_std_17
    LINKS wolf
    INCLUDE_PRIVATE *
)

if(WOLF_INSTALL)
    qm_add_copy_command(${PROJECT_NAME}
        SOURCES plugin.json
        INSTALL_DIR .
    )
else()
    qm_add_copy_command(${PROJECT_NAME}
        SKIP_INSTALL
        SOURCES plugin.json
    )
endif()
