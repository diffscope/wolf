project(wolfpinyin
    VERSION ${WOLF_VERSION}
    LANGUAGES CXX
)

find_package(cpp-pinyin CONFIG REQUIRED)

wolf_add_interpreter_plugin(${PROJECT_NAME} LINKS_PRIVATE cpp-pinyin::cpp-pinyin)
