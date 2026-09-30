project(wolflua
    VERSION ${WOLF_VERSION}
    LANGUAGES CXX
)

wolf_add_interpreter_plugin(${PROJECT_NAME} LINKS_PRIVATE wolf::luajit)
