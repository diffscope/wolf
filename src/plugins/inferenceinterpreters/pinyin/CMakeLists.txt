project(wolfpinyin
    VERSION ${WOLF_VERSION}
    LANGUAGES CXX
)

find_package(cpp-pinyin CONFIG REQUIRED)

file(GLOB_RECURSE _src *.h *.cpp)

wolf_add_plugin(${PROJECT_NAME} CATEGORY ${CURRENT_PLUGIN_CATEGORY}/pinyin NO_EXPORT
    SOURCES ${_src}
    FEATURES cxx_std_17
    LINKS wolf
    LINKS_PRIVATE cpp-pinyin::cpp-pinyin
    INCLUDE_PRIVATE *
)
wolf_drop_plugin_postfix(${PROJECT_NAME})

stdc_add_plugin_metadata(TARGET ${PROJECT_NAME} IID ${CURRENT_PLUGIN_IID})

configure_file(plugin.json
    "${WOLF_BUILD_PLUGINS_DIR}/${CURRENT_PLUGIN_CATEGORY}/pinyin/plugin.json"
    COPYONLY
)

if(WOLF_INSTALL)
    install(FILES plugin.json
        DESTINATION "${WOLF_INSTALL_PLUGINS_DIR}/${CURRENT_PLUGIN_CATEGORY}/pinyin"
    )
endif()
