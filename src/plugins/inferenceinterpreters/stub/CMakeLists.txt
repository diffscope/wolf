project(wolfstubinference
    VERSION ${WOLF_VERSION}
    LANGUAGES CXX
)

# Test scaffolding: not installed, and built only with the tests.
wolf_add_interpreter_plugin(${PROJECT_NAME} NO_INSTALL NO_MANIFEST)

# The stub replaces variants that have no real interpreter in this build, so the variants it
# declares depend on the contents of the build. Otherwise two providers for one triple would both
# be discoverable, and the selected provider would depend on directory order.
#
# plugin.json.in lists every variant that the stub can declare, and the entries for which this
# build has a real interpreter are removed here, so that the manifest is maintained as a single
# JSON file instead of being assembled from string fragments. One entry always remains:
# stub-miscount, a variant that no real interpreter declares, so it never competes for a triple in
# either build. It allows a fixture to request a module that intentionally violates the provider
# ABI by returning a shorter batch than its input; three layers now reject such a module, and no
# shipped module can produce one.
set(_stub_real_variants "")
if(WOLF_HAS_DSINFER)
    list(APPEND _stub_real_variants multig2p-onnx)
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/plugin.json.in")
file(READ plugin.json.in _stub_manifest)
string(JSON _stub_count LENGTH "${_stub_manifest}" interpreters)
math(EXPR _stub_index "${_stub_count} - 1")
# The entries are visited from the end, so that removing an entry does not shift the entries not
# yet visited.
while(_stub_index GREATER_EQUAL 0)
    string(JSON _stub_variant GET "${_stub_manifest}" interpreters ${_stub_index} variant)
    if(_stub_variant IN_LIST _stub_real_variants)
        string(JSON _stub_manifest REMOVE "${_stub_manifest}" interpreters ${_stub_index})
    endif()
    math(EXPR _stub_index "${_stub_index} - 1")
endwhile()

# file(CONFIGURE) writes the file only if the content changes, so a reconfigure leaves an
# unchanged manifest untouched.
file(CONFIGURE
    OUTPUT "${WOLF_BUILD_PLUGINS_DIR}/${CURRENT_PLUGIN_CATEGORY}/stub/plugin.json"
    CONTENT "${_stub_manifest}\n"
)

unset(_stub_real_variants)
unset(_stub_manifest)
unset(_stub_count)
unset(_stub_index)
unset(_stub_variant)
