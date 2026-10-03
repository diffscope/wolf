get_filename_component(_dir_name ${CMAKE_CURRENT_LIST_DIR} NAME)
set(CURRENT_PLUGIN_CATEGORY ${_dir_name})
# The IID is defined once, by LinguistProviderPlugin::IID in the public header, and parsed from
# there: the metadata written into every provider plugin must contain exactly the string that the
# category loader compares against, and two copies of the string could diverge without a build
# failure.
file(STRINGS "${WOLF_SOURCE_DIR}/include/wolf/Linguist/LinguistProviderPlugin.h" _iid_line
    REGEX "IID = \"[^\"]+\""
)
if(NOT _iid_line MATCHES "IID = \"([^\"]+)\"")
    message(FATAL_ERROR
        "LinguistProviderPlugin.h does not define IID in the form that this file parses")
endif()
set(CURRENT_PLUGIN_IID "${CMAKE_MATCH_1}")
unset(_iid_line)

add_subdirectory(wolf)
