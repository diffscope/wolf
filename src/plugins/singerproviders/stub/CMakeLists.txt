project(wolfstubsinger
    VERSION ${WOLF_VERSION}
    LANGUAGES CXX
)

file(GLOB_RECURSE _src *.h *.cpp)

# Test scaffolding: never installed, and built only when the tests are.
wolf_add_plugin(${PROJECT_NAME} CATEGORY ${CURRENT_PLUGIN_CATEGORY}/stub NO_EXPORT NO_INSTALL
    SOURCES ${_src}
    FEATURES cxx_std_17
    LINKS wolf
    INCLUDE_PRIVATE *
)
wolf_drop_plugin_postfix(${PROJECT_NAME})

stdc_add_plugin_metadata(TARGET ${PROJECT_NAME} IID ${CURRENT_PLUGIN_IID})

configure_file(plugin.json
    "${WOLF_BUILD_PLUGINS_DIR}/${CURRENT_PLUGIN_CATEGORY}/stub/plugin.json"
    COPYONLY
)
