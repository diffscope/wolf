project(cmn
    VERSION ${WOLF_VERSION}
    LANGUAGES CXX
)

file(GLOB_RECURSE _src *.h *.cpp)

wolf_add_plugin(${PROJECT_NAME} CATEGORY ${CURRENT_PLUGIN_CATEGORY} NO_EXPORT
    SOURCES ${_src}
    FEATURES cxx_std_17
    LINKS wolf
    INCLUDE_PRIVATE *
)
