# This project consumes wolf through the installed CMake package in the same way as a host.
#
# The build of the library itself does not verify that package. The install rules write every path
# in the package, and no step before this project reads those paths. A target file that names a
# library that was not installed, a config file without find_dependency(synthrt), and an include
# directory that points into the build tree all pass the test suite of this repository and fail in
# a host. CI therefore installs wolf and configures this project, and the assertions below check
# the package rather than the code.
cmake_minimum_required(VERSION 3.19)
project(wolf_package_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(synthrt-wolf CONFIG REQUIRED)

if(NOT TARGET synthrt::wolf)
    message(FATAL_ERROR "the synthrt-wolf package defines no synthrt::wolf target")
endif()

# The libraries under src/util contain the stateless helpers that libwolf and the plugins of wolf
# share. They are not installed, and no binary exports their symbols, so a header of theirs in the
# package would declare functions that a host cannot link. Their absence is checked here.
get_target_property(_wolf_include_dirs synthrt::wolf INTERFACE_INCLUDE_DIRECTORIES)
foreach(_dir IN LISTS _wolf_include_dirs)
    foreach(_util IN ITEMS linguistutil textutil executiveutil)
        if(EXISTS "${_dir}/${_util}")
            message(FATAL_ERROR "the wolf package installs ${_util} under ${_dir}")
        endif()
    endforeach()
endforeach()

# Every installed plugin is an imported target of the package. The two plugins below are built in
# every configuration, one in each category in which wolf ships plugins, and the program checks
# that the category directory of each exists, which is the directory a host adds.
foreach(_plugin IN ITEMS synthrt::wolflinguistprovider synthrt::wolfchain)
    if(NOT TARGET ${_plugin})
        message(FATAL_ERROR "the synthrt-wolf package defines no ${_plugin} target")
    endif()
endforeach()

add_executable(wolf_package_consumer main.cpp)
target_link_libraries(wolf_package_consumer PRIVATE synthrt::wolf)
target_compile_definitions(wolf_package_consumer PRIVATE
    WOLF_CONSUMER_LINGUIST_PLUGIN_DIR="$<TARGET_FILE_DIR:synthrt::wolflinguistprovider>/.."
    WOLF_CONSUMER_INFERENCE_PLUGIN_DIR="$<TARGET_FILE_DIR:synthrt::wolfchain>/.."
)
