add_auto_test(test_LanguageContrib.cpp wolf)

add_auto_test(test_LanguageProvider.cpp wolf)

set_tests_properties(test_LanguageContrib test_LanguageProvider PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:synthrt::synthrt>"
)

if(TARGET test_LanguageProvider)
    add_dependencies(test_LanguageProvider wolflanguageprovider)
    target_compile_definitions(test_LanguageProvider PRIVATE
        WOLF_TEST_PLUGIN_FILE="$<TARGET_FILE:wolflanguageprovider>"
        WOLF_TEST_PLUGIN_DIR="$<TARGET_FILE_DIR:wolflanguageprovider>"
    )
endif()
