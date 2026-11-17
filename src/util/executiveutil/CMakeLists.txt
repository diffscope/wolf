project(executiveutil
    VERSION ${WOLF_VERSION}
    LANGUAGES CXX
)

# The bases of the executives of the plugins, which hold the task face and the stop flag. The
# library consists of headers only.
add_library(${PROJECT_NAME} INTERFACE)
target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_17)
target_link_libraries(${PROJECT_NAME} INTERFACE synthrt::synthrt)
target_include_directories(${PROJECT_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
