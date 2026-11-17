project(wolfmultig2p
    VERSION ${WOLF_VERSION}
    LANGUAGES CXX
)

# Decoder.h declares its sessions with the types of dsinfer, so dsinfer is a public dependency.
wolf_add_interpreter_plugin(${PROJECT_NAME} LINKS synthrt::dsinfer LINKS_PRIVATE linguistutil textutil executiveutil)
