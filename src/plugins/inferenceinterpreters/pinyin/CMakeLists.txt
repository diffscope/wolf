project(wolfpinyin
    VERSION ${WOLF_VERSION}
    LANGUAGES CXX
)

find_package(cpp-pinyin2 CONFIG REQUIRED)

wolf_add_interpreter_plugin(${PROJECT_NAME} LINKS_PRIVATE cpp-pinyin2::cpp-pinyin2)
