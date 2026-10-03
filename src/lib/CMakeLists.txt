file(GLOB_RECURSE _src "*.cpp")

# The stateless Support helpers are compiled into a static library rather than into libwolf, and
# libwolf and every plugin in this repository link it privately. Its objects are built with hidden
# visibility, so none of them is exported from any binary: the helpers are internal tools of the
# library, their headers are not installed, and a symbol that no consumer may use must not be
# exported. Each plugin carries its own copy, which is safe because none of the helpers holds
# state.
#
# The components that hold state which must be shared stay in libwolf and are exported under
# WOLF_INTERNAL_EXPORT (Support/SupportGlobal.h): the resource cache, which must be unique per
# process, and the log category, which is registered by name.
set(_wolf_support_sources
    Support/ContractValues.cpp
    Support/Files.cpp
    Support/InputRules.cpp
    Support/ManifestValues.cpp
    Support/Utf8.cpp
    Support/Verifier.cpp
)
list(TRANSFORM _wolf_support_sources PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
list(REMOVE_ITEM _src ${_wolf_support_sources})

add_library(wolfsupport STATIC ${_wolf_support_sources})
target_compile_features(wolfsupport PUBLIC cxx_std_17)
target_include_directories(wolfsupport
    PUBLIC "${WOLF_SOURCE_DIR}/include"
    PRIVATE "${WOLF_SOURCE_DIR}/include/wolf/Support"
)
target_link_libraries(wolfsupport PUBLIC synthrt::synthrt PRIVATE re2::re2)
set_target_properties(wolfsupport PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

wolf_add_library(${PROJECT_NAME} SHARED
    SOURCES ${_src}
    LINKS synthrt::synthrt
    LINKS_PRIVATE BLAKE3::blake3 wolfsupport
    INCLUDE_PRIVATE ../../include/** **
)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME synthrt-wolf)

# Linking wolf provides a static registration that runs before main and adds the linguist category
# to the synthrt registry. A linker that drops unreferenced libraries, as ELF linkers do under
# --as-needed and the MSVC linker does for every import library, would drop exactly that
# dependency from a host that references no wolf symbol. Instead of imposing a linker option on
# the link line of every consumer, the library exports wolf::linkLinguistCategory() for such a host
# to call.

# blake3 and re2 are private: neither appears in a public header (blake3 only in
# Support/ResourceCache.cpp, re2 only in Support/Verifier.cpp, which is compiled into the static
# wolfsupport library), so neither belongs in the exported link interface. wolfConfig.cmake.in
# declares only find_dependency(synthrt), and an exported link interface must not name a target
# that the config does not import; otherwise find_package(wolf) fails at generate time with "but
# the target was not found" for every consumer that has not found that target itself. The tests
# link the build-tree target and do not use the installed config, so the check below keeps such a
# target out of the interface.
set(_wolf_exported_targets synthrt::synthrt)  # keep in sync with src/wolfConfig.cmake.in
get_target_property(_wolf_interface_links ${PROJECT_NAME} INTERFACE_LINK_LIBRARIES)

foreach(_link IN LISTS _wolf_interface_links)
    if(NOT _link MATCHES "::")
        continue()  # a plain library name or a flag, not an imported target
    endif()
    if(_link IN_LIST _wolf_exported_targets)
        continue()
    endif()
    message(FATAL_ERROR
        "the link interface of wolf contains '${_link}', but src/wolfConfig.cmake.in does not "
        "import that target. Move the dependency to LINKS_PRIVATE if it appears in no public "
        "header, or add a find_dependency() call for it to the config template and add it to "
        "_wolf_exported_targets in src/lib/CMakeLists.txt.")
endforeach()

unset(_wolf_exported_targets)
unset(_wolf_interface_links)
