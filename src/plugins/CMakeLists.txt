# A plugin's file name is what its plugin.json names, and the bundle loader tries only the
# platform's prefix and suffix around that name. The debug postfix the library targets carry would
# turn wolfchain.dll into wolfchaind.dll and leave every plugin undiscoverable in a Debug build, so
# every plugin drops it right after it is declared.
function(wolf_drop_plugin_postfix _target)
    set_target_properties(${_target} PROPERTIES DEBUG_POSTFIX "")
endfunction()

add_subdirectory(linguistproviders)

add_subdirectory(inferenceinterpreters)

# A stub singer provider only exists so the linguist domain can be tested without a voicebank.
if(WOLF_BUILD_TESTS)
    add_subdirectory(singerproviders)
endif()
