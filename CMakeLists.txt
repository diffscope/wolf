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
# Tests
# ----------------------------------
if(WOLF_BUILD_TESTS)
    enable_testing()
endif()

# ----------------------------------
# Add source modules
# ----------------------------------
add_subdirectory(src)
