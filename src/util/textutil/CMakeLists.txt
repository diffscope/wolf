project(textutil
    VERSION ${WOLF_VERSION}
    LANGUAGES CXX
)

file(GLOB_RECURSE _src *.h *.cpp)

# The reading of the text resources that libwolf and the plugins load: the errors of a file that
# cannot be read, the line decorations of a resource, and UTF-8. The library is static and not
# installed, and its objects are built with hidden visibility, so that no binary exports them.
wolf_add_library(${PROJECT_NAME} STATIC NO_INSTALL
    SOURCES ${_src}
    FEATURES cxx_std_17
    LINKS synthrt::synthrt
    INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/..
)
set_target_properties(${PROJECT_NAME} PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
