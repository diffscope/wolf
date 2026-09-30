project(wolfmultig2p
    VERSION ${WOLF_VERSION}
    LANGUAGES CXX
)

wolf_add_interpreter_plugin(${PROJECT_NAME} LINKS_PRIVATE dsinfer::dsinfer)
