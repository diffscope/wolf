project(wolfstubsinger
    VERSION ${WOLF_VERSION}
    LANGUAGES CXX
)

# Test scaffolding: not installed, and built only with the tests.
wolf_add_interpreter_plugin(${PROJECT_NAME} NO_INSTALL)
