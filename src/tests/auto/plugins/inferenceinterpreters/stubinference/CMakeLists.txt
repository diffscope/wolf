# The stub interpreters, which let the linguist domain be tested without a model. They are a static
# plugin of the test executables that link these objects, ahead of the plugins of the search paths.
# The library is an object library rather than a static one, because nothing references the
# registration, and a linker drops a member of an archive that nothing references.
add_library(wolfstubinference OBJECT
    main.cpp
    StubExecutives.cpp
    StubExecutives.h
)
target_compile_features(wolfstubinference PUBLIC cxx_std_17)
target_link_libraries(wolfstubinference PUBLIC synthrt-wolf PRIVATE linguistutil executiveutil)

# The variants that a real interpreter of this build serves are left out of the declarations of the
# stub, which would otherwise take them from the real interpreter.
if(TARGET wolfmultig2p)
    target_compile_definitions(wolfstubinference PRIVATE WOLF_TEST_HAS_MULTIG2P=1)
endif()
