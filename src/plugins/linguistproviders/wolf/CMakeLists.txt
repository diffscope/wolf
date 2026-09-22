project(wolflinguistprovider
    VERSION ${WOLF_VERSION}
    LANGUAGES CXX
)

file(GLOB_RECURSE _src *.h *.cpp)

wolf_add_plugin(${PROJECT_NAME} CATEGORY ${CURRENT_PLUGIN_CATEGORY}/wolf NO_EXPORT
    SOURCES ${_src}
    FEATURES cxx_std_17
    LINKS wolf
    INCLUDE_PRIVATE *
)
wolf_drop_plugin_postfix(${PROJECT_NAME})

stdc_add_plugin_metadata(TARGET ${PROJECT_NAME} IID ${CURRENT_PLUGIN_IID})

# configure_file rather than a copy command: it re-runs when plugin.json changes and restores the
# output if it goes missing, neither of which the copy command does. A stale manifest is not a
# harmless one, because discovery resolves the library through its name field.
configure_file(plugin.json
    "${WOLF_BUILD_PLUGINS_DIR}/${CURRENT_PLUGIN_CATEGORY}/wolf/plugin.json"
    COPYONLY
)

if(WOLF_INSTALL)
    install(FILES plugin.json
        DESTINATION "${WOLF_INSTALL_PLUGINS_DIR}/${CURRENT_PLUGIN_CATEGORY}/wolf"
    )
endif()
