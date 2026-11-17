# The stub singer provider, which lets the linguist domain be tested without a voicebank. It is a
# static plugin of the test executables that link these objects, ahead of the plugins of the search
# paths. The library is an object library for the reason given beside the stub interpreters.
add_library(wolfstubsinger OBJECT
    main.cpp
    StubSingerProvider.cpp
    StubSingerProvider.h
)
target_compile_features(wolfstubsinger PUBLIC cxx_std_17)
target_link_libraries(wolfstubsinger PUBLIC synthrt-wolf)
