add_auto_test(test_LanguageContrib.cpp wolf)

add_auto_test(test_MandarinProvider.cpp wolf)

if(TARGET test_MandarinProvider)
    # The test loads the plugin the way a program would, by path, so it has to be told where the
    # build put it.
    add_dependencies(test_MandarinProvider cmn)
    target_compile_definitions(test_MandarinProvider PRIVATE
        WOLF_TEST_PLUGIN_DIR="$<TARGET_FILE_DIR:cmn>"
    )
endif()
