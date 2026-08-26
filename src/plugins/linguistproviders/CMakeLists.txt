get_filename_component(_dir_name ${CMAKE_CURRENT_LIST_DIR} NAME)
set(CURRENT_PLUGIN_CATEGORY ${_dir_name})
set(CURRENT_PLUGIN_IID org.openvpi.wolf.plugin.LinguistProvider)

add_subdirectory(wolf)
