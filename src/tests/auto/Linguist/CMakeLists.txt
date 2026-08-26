add_auto_test(test_LinguistContrib.cpp wolf)

add_auto_test(test_LinguistProvider.cpp wolf)

set_tests_properties(test_LinguistContrib test_LinguistProvider PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:synthrt::synthrt>"
)

if(TARGET test_LinguistProvider)
    add_dependencies(test_LinguistProvider wolflinguistprovider)
    target_compile_definitions(test_LinguistProvider PRIVATE
        WOLF_TEST_PLUGIN_FILE="$<TARGET_FILE:wolflinguistprovider>"
        WOLF_TEST_PLUGIN_DIR="$<TARGET_FILE_DIR:wolflinguistprovider>"
    )
endif()
