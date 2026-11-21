file(GLOB_RECURSE _src "*.cpp")

# The components that hold state which must be shared are part of libwolf and are exported with
# WOLF_EXPORT (Support/): the resource cache, which must be unique per process, and the log
# category, which is registered by name. The stateless helpers are in the static libraries under
# src/util, which libwolf and the plugins in this repository link privately.

# The export macros would take their prefix from the target name, SYNTHRT_WOLF, while
# wolf_global.h tests WOLF_LIBRARY and WOLF_STATIC.
wolf_add_library(${PROJECT_NAME} SHARED
    MACRO_PREFIX WOLF
    SOURCES ${_src}
    LINKS synthrt::synthrt synthrt::dsinfer
    LINKS_PRIVATE BLAKE3::blake3 linguistutil textutil
    INCLUDE_PRIVATE ../../include/** **
)

# The package is synthrt-wolf, and the namespace is synthrt, which the libraries built on synthrt
# share, so that a host links synthrt::wolf.
set_target_properties(${PROJECT_NAME} PROPERTIES EXPORT_NAME wolf)

# Linking wolf provides a static registration that runs before main and adds the linguist category
# to the synthrt registry. A linker that drops unreferenced libraries, as ELF linkers do under
# --as-needed and the MSVC linker does for every import library, would drop exactly that
# dependency from a host that references no wolf symbol. Instead of imposing a linker option on
# the link line of every consumer, the library exports wolf::linkLinguistCategory() for such a host
# to call.

# blake3 and re2 are private: neither appears in a public header (blake3 only in
# Support/ResourceCache.cpp, re2 only in Verifier.cpp of the static linguistutil library), so neither
# belongs in the exported link interface.
# synthrt-wolf-config.cmake.in declares only find_dependency(synthrt), and an exported link
# interface must not name a target that the config does not import; otherwise
# find_package(synthrt-wolf) fails at generate time with "but the target was not found" for every
# consumer that has not found that target itself. The tests link the build-tree target and do not
# use the installed config, so the check below keeps such a target out of the interface.
set(_wolf_exported_targets synthrt::synthrt synthrt::dsinfer)  # keep in sync with src/synthrt-wolf-config.cmake.in
get_target_property(_wolf_interface_links ${PROJECT_NAME} INTERFACE_LINK_LIBRARIES)

foreach(_link IN LISTS _wolf_interface_links)
    if(NOT _link MATCHES "::")
        continue()  # a plain library name or a flag, not an imported target
    endif()
    if(_link IN_LIST _wolf_exported_targets)
        continue()
    endif()
    message(FATAL_ERROR
        "the link interface of wolf contains '${_link}', but src/synthrt-wolf-config.cmake.in "
        "does not import that target. Move the dependency to LINKS_PRIVATE if it appears in no "
        "public header, or add a find_dependency() call for it to the config template and add it "
        "to _wolf_exported_targets in src/lib/CMakeLists.txt.")
endforeach()

unset(_wolf_exported_targets)
unset(_wolf_interface_links)
