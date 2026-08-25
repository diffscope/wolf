file(GLOB_RECURSE _src "*.cpp")

wolf_add_library(${PROJECT_NAME} SHARED
    SOURCES ${_src}
    LINKS synthrt::synthrt
    INCLUDE_PRIVATE ../../include/** **
)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME synthrt-wolf)