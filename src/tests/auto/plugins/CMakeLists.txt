# The tests of single plugins, one directory per plugin. A test covers one header of the plugin and
# links the objects of the plugin, <target>_objects, as the test of a class of a library links the
# library.
#
# A test of an interpreter registers the plugin of the library as a static plugin, under the
# plugin.json that the library ships, so that a package loads the interpreter linked into the test
# rather than the built library. A static plugin precedes the plugins of the search paths, so the
# plugin directories that a test passes for the other stages of a tree do not shadow it.
set(_wolf_plugin_tests "")

# The plugins that the tests use in place of a model or a voicebank, one directory per plugin
add_subdirectory(inferenceinterpreters/stubinference)
add_subdirectory(singerproviders/stubsinger)

# wolf_add_plugin_test(<source> <plugin> [extra link libraries...])
#
# Registers the test <source> of the plugin <plugin>, which links <plugin>_objects, includes
# TestSupport.h, and receives the manifest of the plugin as WOLF_TEST_PLUGIN_MANIFEST.
macro(wolf_add_plugin_test _source _plugin)
    add_auto_test(${_source} ${_plugin}_objects ${ARGN})
    get_filename_component(_wolf_test_name ${_source} NAME_WE)
    if(TARGET ${_wolf_test_name})
        get_target_property(_wolf_plugin_dir ${_plugin} SOURCE_DIR)
        target_include_directories(${_wolf_test_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
        target_compile_definitions(${_wolf_test_name} PRIVATE
            WOLF_TEST_PLUGIN_MANIFEST="${_wolf_plugin_dir}/plugin.json"
        )
        list(APPEND _wolf_plugin_tests ${_wolf_test_name})
    endif()
endmacro()

# A package of one module resolves the plugin that the test registers and no other, so these tests
# pass no plugin directory.
wolf_add_plugin_test(inferenceinterpreters/onset/test_OnsetInterpreter.cpp wolfonset)
wolf_add_plugin_test(inferenceinterpreters/s2p/test_S2pInterpreter.cpp wolfs2p)
if(TARGET wolflua)
    wolf_add_plugin_test(inferenceinterpreters/lua/test_LuaInterpreters.cpp wolflua)
endif()

# The chain and the engine are exercised through the complete tree: a singer declares a language,
# the language declares a chain, and the chain reaches its backend through an import. The stages
# other than the tested one load from the plugin directories.
set(_wolf_tree_plugins wolflinguistprovider wolfchain wolfpinyin wolfs2p)
foreach(_wolf_tree_test IN ITEMS inferenceinterpreters/chain/test_ChainInterpreter.cpp:wolfchain
                                 inferenceinterpreters/pinyin/test_PinyinInterpreter.cpp:wolfpinyin)
    string(REPLACE ":" ";" _wolf_tree_test "${_wolf_tree_test}")
    list(GET _wolf_tree_test 0 _wolf_tree_source)
    list(GET _wolf_tree_test 1 _wolf_tree_plugin)
    wolf_add_plugin_test(${_wolf_tree_source} ${_wolf_tree_plugin})
    if(TARGET ${_wolf_test_name})
        target_link_libraries(${_wolf_test_name} PRIVATE wolfstubinference wolfstubsinger)
        set(_wolf_tree_dependencies ${_wolf_tree_plugins})
        list(REMOVE_ITEM _wolf_tree_dependencies ${_wolf_tree_plugin})
        add_dependencies(${_wolf_test_name} ${_wolf_tree_dependencies})
        # One case requires a package whose phoneme stage is a script, which cannot load without
        # the scripted variants.
        if(TARGET wolflua)
            add_dependencies(${_wolf_test_name} wolflua)
            target_compile_definitions(${_wolf_test_name} PRIVATE WOLF_TEST_HAS_LUAJIT=1)
        endif()
        target_compile_definitions(${_wolf_test_name} PRIVATE
            WOLF_TEST_LINGUIST_PLUGIN_DIR="${_wolf_linguist_plugins}"
            WOLF_TEST_INFERENCE_PLUGIN_DIR="${_wolf_inference_plugins}"
        )
    endif()
endforeach()

# The text helpers of the cleanup step, which no package reaches.
add_auto_test(inferenceinterpreters/chain/test_ChainTables.cpp wolfchain_objects)
list(APPEND _wolf_plugin_tests test_ChainTables)

if(TARGET wolfmultig2p)
    # The drivers directory of the installed dsinfer, which holds the ONNX driver, empty if dsinfer
    # was built without it
    set(_wolf_driver_plugin_dir "")
    if(TARGET synthrt::onnxdriver)
        set(_wolf_driver_plugin_dir "$<TARGET_FILE_DIR:synthrt::onnxdriver>/..")
    endif()

    # The shared model reaches a language through a chain.
    wolf_add_plugin_test(inferenceinterpreters/multig2p/test_MultiG2pInterpreter.cpp wolfmultig2p)
    if(TARGET test_MultiG2pInterpreter)
        target_link_libraries(test_MultiG2pInterpreter PRIVATE wolfstubinference wolfstubsinger)
        add_dependencies(test_MultiG2pInterpreter wolflinguistprovider wolfchain wolfs2p)
        target_compile_definitions(test_MultiG2pInterpreter PRIVATE
            WOLF_TEST_LINGUIST_PLUGIN_DIR="${_wolf_linguist_plugins}"
            WOLF_TEST_INFERENCE_PLUGIN_DIR="${_wolf_inference_plugins}"
            WOLF_TEST_ONNXRUNTIME_DIR="${WOLF_ONNXRUNTIME_DIR}"
            WOLF_TEST_DRIVER_PLUGIN_DIR="${_wolf_driver_plugin_dir}"
        )
    endif()

    # The limit case binds the backend directly rather than through a chain, so it needs no other
    # plugin: it assembles the smallest tree in which a declaration reaches a model.
    wolf_add_plugin_test(inferenceinterpreters/multig2p/test_MultiG2pInterpreter_MaxLen.cpp wolfmultig2p)
    if(TARGET test_MultiG2pInterpreter_MaxLen)
        target_compile_definitions(test_MultiG2pInterpreter_MaxLen PRIVATE
            WOLF_TEST_ONNXRUNTIME_DIR="${WOLF_ONNXRUNTIME_DIR}"
            WOLF_TEST_DRIVER_PLUGIN_DIR="${_wolf_driver_plugin_dir}"
        )
    endif()

    # The bundle formats of the shared model, whose malformed shapes a package with a working model
    # never shows.
    add_auto_test(inferenceinterpreters/multig2p/test_Bundle.cpp wolfmultig2p_objects)
    add_auto_test(inferenceinterpreters/multig2p/test_Decoder.cpp wolfmultig2p_objects)
    list(APPEND _wolf_plugin_tests test_Decoder)
    list(APPEND _wolf_plugin_tests test_Bundle)
endif()

# Every test runs in the environment of the other tests, which the parent directory computes,
# together with the data variables, the skip status and the timeouts that the parent directory
# describes. The properties are set here because set_tests_properties() reaches only the tests of
# the directory that adds them.
foreach(_helper IN ITEMS inferenceinterpreters/s2p/test_S2pTables.cpp:wolfs2p inferenceinterpreters/onset/test_OnsetRules.cpp:wolfonset inferenceinterpreters/pinyin/test_PinyinEngines.cpp:wolfpinyin inferenceinterpreters/lua/test_LuaSandbox.cpp:wolflua)
    string(REPLACE ":" ";" _helper "${_helper}")
    list(GET _helper 0 _source)
    list(GET _helper 1 _plugin)
    if(TARGET ${_plugin}_objects)
        add_auto_test(${_source} ${_plugin}_objects)
        get_filename_component(_name ${_source} NAME_WE)
        list(APPEND _wolf_plugin_tests ${_name})
    endif()
endforeach()

foreach(_part IN ITEMS WolfLinguistProvider LinguistExecutiveImpl WolfPipelineExecutive)
    wolf_add_plugin_test(linguistproviders/wolflinguist/test_${_part}.cpp wolflinguistprovider wolfstubinference wolfstubsinger executiveutil)
    if(TARGET test_${_part})
        add_dependencies(test_${_part} wolfchain wolfs2p)
        target_compile_definitions(test_${_part} PRIVATE
            WOLF_TEST_INFERENCE_PLUGIN_DIR="${_wolf_inference_plugins}"
        )
        set_tests_properties(test_${_part} PROPERTIES ENVIRONMENT "${_wolf_data_env}" SKIP_RETURN_CODE 77 TIMEOUT 30)
    endif()
endforeach()

foreach(_test IN LISTS _wolf_plugin_tests)
    if(TARGET ${_test})
        set_tests_properties(${_test} PROPERTIES ENVIRONMENT_MODIFICATION "${_wolf_test_env}")
    endif()
endforeach()
foreach(_test IN ITEMS test_MultiG2pInterpreter test_MultiG2pInterpreter_MaxLen test_ChainInterpreter
                       test_PinyinInterpreter)
    if(TARGET ${_test})
        set_tests_properties(${_test} PROPERTIES SKIP_RETURN_CODE 77)
        if(_wolf_data_env)
            set_tests_properties(${_test} PROPERTIES ENVIRONMENT "${_wolf_data_env}")
        endif()
    endif()
endforeach()
foreach(_test IN ITEMS test_ChainInterpreter test_LuaInterpreters)
    if(TARGET ${_test})
        set_tests_properties(${_test} PROPERTIES TIMEOUT 120)
    endif()
endforeach()
