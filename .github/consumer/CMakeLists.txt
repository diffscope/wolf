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

find_package(wolf CONFIG REQUIRED)

if(NOT TARGET wolf::wolf)
    message(FATAL_ERROR "the wolf package defines no wolf::wolf target")
endif()

# The plugin tree is a path rather than a target. No target links against it, so no other check
# detects its absence. A host passes one directory per category to SynthUnit::setPluginPaths, which
# requires the package to export the location of those directories.
if(NOT DEFINED WOLF_PLUGINS_DIR)
    message(FATAL_ERROR "the wolf package exports no WOLF_PLUGINS_DIR")
endif()
# The package must provide one directory for each category in which the library ships plugins. Every
# library that adds inference contracts also uses the inferenceinterpreters category, so a host
# lists the wolf directory next to the directories of those libraries.
foreach(_category IN ITEMS linguistproviders inferenceinterpreters)
    if(NOT IS_DIRECTORY "${WOLF_PLUGINS_DIR}/${_category}")
        message(FATAL_ERROR "WOLF_PLUGINS_DIR (${WOLF_PLUGINS_DIR}) contains no "
                            "${_category} directory")
    endif()
endforeach()
message(STATUS "wolf plugins: ${WOLF_PLUGINS_DIR}")
if(DEFINED WOLF_PLUGINS_DIR_DEBUG)
    message(STATUS "wolf plugins (debug): ${WOLF_PLUGINS_DIR_DEBUG}")
endif()

# wolf/Support contains the internal helpers of the library and is not installed. Its stateless
# helpers are exported from no binary, and the symbols that libwolf exports from it
# (WOLF_INTERNAL_EXPORT) serve only the plugins built with wolf and carry no API or ABI guarantee. A
# Support header in the package would expose exactly that surface to a host, so its absence is
# checked here.
get_target_property(_wolf_include_dirs wolf::wolf INTERFACE_INCLUDE_DIRECTORIES)
foreach(_dir IN LISTS _wolf_include_dirs)
    if(EXISTS "${_dir}/wolf/Support")
        message(FATAL_ERROR "the wolf package installs wolf/Support under ${_dir}")
    endif()
endforeach()

add_executable(wolf_package_consumer main.cpp)
target_link_libraries(wolf_package_consumer PRIVATE wolf::wolf)
