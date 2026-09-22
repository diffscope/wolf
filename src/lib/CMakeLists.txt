file(GLOB_RECURSE _src "*.cpp")

wolf_add_library(${PROJECT_NAME} SHARED
    SOURCES ${_src}
    LINKS synthrt::synthrt
    LINKS_PRIVATE BLAKE3::blake3 re2::re2
    INCLUDE_PRIVATE ../../include/** **
)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME synthrt-wolf)

# What linking wolf buys is a static registration that runs before main: the linguist category is
# added to synthrt's registry. A linker that drops unreferenced libraries, which ELF linkers do
# under --as-needed and the MSVC linker does for every import library, would drop exactly that
# dependency from a host that names no wolf symbol. Rather than push a linker option onto every
# consumer's link line, the library exports wolf::linkLinguistCategory() for such a host to call.

# blake3 and re2 are private: neither appears in a public header (blake3 only in
# Support/ResourceCache.cpp, re2 only in Support/Verifier.cpp), so neither belongs in the exported
# link interface. Exporting blake3 is what made find_package(wolf) fail outright for any consumer
# that had not itself called find_package(blake3) first — wolfConfig.cmake.in only ever declared
# find_dependency(synthrt), and a link interface may not name a target the config does not bring in.
# Guard against the regression this comment describes coming back. A target named in the exported
# link interface must be a target wolfConfig.cmake actually brings in, otherwise every consumer's
# find_package(wolf) fails at generate time with "but the target was not found" — and nothing in
# this repository would notice, because the tests link the build-tree target directly and never go
# through the installed config.
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
        "wolf exports '${_link}' in its link interface, but src/wolfConfig.cmake.in does not "
        "bring that target in. Either move the dependency to LINKS_PRIVATE (correct when it "
        "appears in no public header), or add a find_dependency() for it to the config template "
        "and list it in _wolf_exported_targets here.")
endforeach()

unset(_wolf_exported_targets)
unset(_wolf_interface_links)
