cmake_minimum_required(VERSION 3.19)

# 0.1.0.0 carries the first batch of changes to public value types — LanguageStatus grew a
# reachable depth and a coverage report, and the pipeline extension grew two virtuals. None of that
# is ABI compatible with 0.0.1.0, and wolf makes no ABI promise before 1.0, so the number moves
# with the change rather than after it. See README, "Versioning and ABI".
project(wolf
    VERSION 0.1.0.0
    LANGUAGES CXX
    DESCRIPTION "Orchestratable Linguistic Framework"
    HOMEPAGE_URL "https://github.com/diffscope/wolf"
)

# ----------------------------------
# Build Options
# ----------------------------------
option(WOLF_BUILD_TESTS "Build test cases" OFF)
option(WOLF_INSTALL "Install library" ON)

# ----------------------------------
# CMake Settings
# ----------------------------------
if(MSVC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /manifest:no")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /manifest:no")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /manifest:no")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")

    if(NOT DEFINED CMAKE_DEBUG_POSTFIX)
        set(CMAKE_DEBUG_POSTFIX "d")
    endif()
elseif(MINGW)
    set(CMAKE_STATIC_LIBRARY_PREFIX "")
    set(CMAKE_SHARED_LIBRARY_PREFIX "")
elseif(WIN32)
    if(NOT DEFINED CMAKE_DEBUG_POSTFIX)
        set(CMAKE_DEBUG_POSTFIX "d")
    endif()
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
endif()

if(WOLF_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)
endif()

# ----------------------------------
# Project Variables
# ----------------------------------
set(WOLF_VERSION ${PROJECT_VERSION})
set(WOLF_INSTALL_NAME ${PROJECT_NAME})
set(WOLF_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# ----------------------------------
# Find basic dependencies
# ----------------------------------
find_package(qmsetup REQUIRED)

qm_import(Filesystem)

if(NOT DEFINED QMSETUP_BUILD_DIR)
    set(QMSETUP_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/out")
endif()

qm_init_directories()

find_package(synthrt CONFIG REQUIRED)
find_package(blake3 CONFIG REQUIRED)
find_package(re2 CONFIG REQUIRED)

# ----------------------------------
# Optional inference backend
# ----------------------------------
# The model backed G2P variant runs its models through dsinfer's driver framework. dsinfer is
# built by synthrt[onnx] and installs its own CMake package beside synthrt's, so it is found the
# ordinary way. It is optional, and it can be refused even when it is installed: without that
# there is no supported way to ask for the minimal build.
option(WOLF_DISABLE_DSINFER "Build without dsinfer even if it is installed" OFF)
option(WOLF_DISABLE_LUAJIT "Build without LuaJIT even if it is installed" OFF)

if(NOT WOLF_DISABLE_DSINFER)
    find_package(dsinfer CONFIG QUIET)
endif()

if(TARGET dsinfer::dsinfer)
    set(WOLF_HAS_DSINFER ON)
    # The ONNX driver ships as a plugin under lib/plugins beside the library, not as a link
    # target. The dsinfer package says where that tree is through DSINFER_PLUGINS_DIR; an older
    # package that does not is searched for a lib/plugins/dsinfer tree above its config file,
    # which is share/dsinfer in a vcpkg tree and lib/cmake/dsinfer in a plain install. A plugin
    # search path holds one subdirectory per plugin, so this is the parent of the onnx directory.
    # A vcpkg tree keeps its Debug build under a separate debug/ prefix. Tests hand the directory
    # to the driver factory at run time.
    set(_wolf_dsinfer_plugins "")
    if(DEFINED DSINFER_PLUGINS_DIR AND IS_DIRECTORY "${DSINFER_PLUGINS_DIR}")
        set(_wolf_dsinfer_plugins "${DSINFER_PLUGINS_DIR}")
    else()
        set(_wolf_dsinfer_prefix "${dsinfer_DIR}")
        foreach(_step RANGE 3)
            get_filename_component(_wolf_dsinfer_prefix "${_wolf_dsinfer_prefix}" DIRECTORY)
            if(IS_DIRECTORY "${_wolf_dsinfer_prefix}/lib/plugins/dsinfer")
                set(_wolf_dsinfer_plugins "${_wolf_dsinfer_prefix}/lib/plugins/dsinfer")
                break()
            endif()
        endforeach()
    endif()
    set(WOLF_DSINFER_DRIVER_DIR "")
    if(_wolf_dsinfer_plugins)
        get_filename_component(_wolf_dsinfer_prefix "${_wolf_dsinfer_plugins}/../.." ABSOLUTE)
        if(IS_DIRECTORY "${_wolf_dsinfer_prefix}/debug/lib/plugins/dsinfer")
            set(WOLF_DSINFER_DRIVER_DIR
                "${_wolf_dsinfer_prefix}/$<$<CONFIG:Debug>:debug/>lib/plugins/dsinfer/inferencedrivers")
        else()
            set(WOLF_DSINFER_DRIVER_DIR "${_wolf_dsinfer_plugins}/inferencedrivers")
        endif()
    else()
        message(WARNING "dsinfer was found but its plugin tree was not; the model backed tests "
                        "will skip for want of a driver")
    endif()
    message(STATUS "dsinfer: ${dsinfer_DIR}")
else()
    set(WOLF_HAS_DSINFER OFF)
    if(WOLF_DISABLE_DSINFER)
        message(STATUS "dsinfer: disabled; the model backed G2P variant will not be built")
    else()
        message(STATUS "dsinfer: not found; the model backed G2P variant will not be built "
                       "(install synthrt with the onnx feature to enable it)")
    endif()
endif()

# The scripted variants embed LuaJIT, which ships a pkg-config file and no CMake package, so it
# too is located by hand. Its headers live in a versioned subdirectory.
# The versioned subdirectory is what the Unix makefile installs to; the vcpkg port on Windows uses
# a plain luajit directory instead.
find_path(WOLF_LUAJIT_INCLUDE_DIR
    NAMES lua.hpp
    PATH_SUFFIXES luajit-2.1 luajit
)
find_library(WOLF_LUAJIT_LIBRARY
    NAMES luajit-5.1 lua51 luajit
)

if(WOLF_LUAJIT_INCLUDE_DIR AND WOLF_LUAJIT_LIBRARY AND NOT WOLF_DISABLE_LUAJIT)
    set(WOLF_HAS_LUAJIT ON)
    if(NOT TARGET wolf::luajit)
        add_library(wolf::luajit UNKNOWN IMPORTED)
        set_target_properties(wolf::luajit PROPERTIES
            IMPORTED_LOCATION "${WOLF_LUAJIT_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${WOLF_LUAJIT_INCLUDE_DIR}"
        )
    endif()
    message(STATUS "LuaJIT: ${WOLF_LUAJIT_LIBRARY}")
else()
    set(WOLF_HAS_LUAJIT OFF)
    if(WOLF_DISABLE_LUAJIT)
        message(STATUS "LuaJIT: disabled; the scripted variants will not be built")
    else()
        message(STATUS "LuaJIT: not found; the scripted variants will not be built")
    endif()
endif()

# Where the ONNX Runtime payload was deployed, so tests can hand it to the driver. The driver
# loads the library itself at run time, which is why this is a directory and not a link target.
find_package(onnxruntime-builds CONFIG QUIET)
if(onnxruntime-builds_FOUND)
    set(WOLF_ONNXRUNTIME_DIR "${ONNXRUNTIME_BUILDS_RUNTIME_DIR}")
    message(STATUS "ONNX Runtime: ${WOLF_ONNXRUNTIME_DIR}")
else()
    set(WOLF_ONNXRUNTIME_DIR "")
endif()

# ----------------------------------
# Language package data
# ----------------------------------
# Tests that load real packages need the resources. They normally arrive through the
# wolf-lang-packages port, but a local unpacked copy overrides it: that is how the packages stay
# usable while the release they come from is not publicly reachable, without any port or consumer
# having to hold a credential.
set(WOLF_LANG_PACKAGES_SOURCE "" CACHE PATH
    "Unpacked language packages to use instead of the wolf-lang-packages port")

# The voicebank shaped fixture scripts/make-voicebank-fixture.py generates from those packages.
# Only the host flow test reads it, and that test skips itself when this is empty.
set(WOLF_VOICEBANK_FIXTURE_SOURCE "" CACHE PATH
    "Generated voicebank fixture for the host flow test")

if(NOT WOLF_LANG_PACKAGES_SOURCE)
    find_package(wolf-lang-packages CONFIG QUIET)
    if(wolf-lang-packages_FOUND)
        set(WOLF_LANG_PACKAGES_SOURCE "${WOLF_LANG_PACKAGES_DIR}")
    endif()
endif()

if(WOLF_LANG_PACKAGES_SOURCE)
    message(STATUS "Language packages: ${WOLF_LANG_PACKAGES_SOURCE}")
else()
    message(STATUS "Language packages: none; tests that need them will skip")
endif()

# ----------------------------------
# Tests
# ----------------------------------
if(WOLF_BUILD_TESTS)
    enable_testing()
endif()

# ----------------------------------
# Add source modules
# ----------------------------------
add_subdirectory(src)
