project(linguistutil
    VERSION ${WOLF_VERSION}
    LANGUAGES CXX
)

file(GLOB_RECURSE _src *.h *.cpp)

# The readers and the checks of the linguist contracts that libwolf and the plugins share: the
# manifest and contract values, the verifier and the input rules. The library is static and not
# installed, and its objects are built with hidden visibility, so that no binary exports them. Each
# plugin carries its own copy, which is safe because none of the helpers holds state. The headers
# include the API headers of wolf, which this library reaches through the include directory rather
# than by linking libwolf, because libwolf links it.
wolf_add_library(${PROJECT_NAME} STATIC NO_INSTALL
    SOURCES ${_src}
    FEATURES cxx_std_17
    LINKS synthrt::synthrt
    LINKS_PRIVATE re2::re2 textutil
    INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${WOLF_SOURCE_DIR}/include
)
set_target_properties(${PROJECT_NAME} PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)
