add_auto_test(test_LanguageContrib.cpp wolf)

add_auto_test(test_LanguageInterpreter.cpp wolf)

set_tests_properties(test_LanguageContrib test_LanguageInterpreter PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:synthrt::synthrt>"
)

if(TARGET test_LanguageInterpreter)
    add_dependencies(test_LanguageInterpreter language)
    target_compile_definitions(test_LanguageInterpreter PRIVATE
        WOLF_TEST_PLUGIN_FILE="$<TARGET_FILE:language>"
        WOLF_TEST_PLUGIN_DIR="$<TARGET_FILE_DIR:language>"
    )
endif()
