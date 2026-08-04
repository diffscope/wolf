file(GLOB_RECURSE _src "*.cpp")

wolf_add_library(${PROJECT_NAME} SHARED
    SOURCES ${_src}
    LINKS synthrt::synthrt
    INCLUDE_PRIVATE ../../include/** **
)
