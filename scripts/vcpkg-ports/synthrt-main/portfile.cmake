# synthrt, main line: the contribution framework on which wolf is built (ContribCategory,
# PackageLoader, SingerCategory). The synthrt port of the shared overlay pins the refactor line
# instead. The description in vcpkg.json records the reason this port has a different name instead
# of shadowing the shared port.

# The onnxruntime-builds-uptake branch carries the ONNX Runtime package uptake and the singer
# category fields wolf reads (languages, defaultLanguage, reservedPhonemes), and the public names of
# the built-in categories. The pin tracks the tip of that branch so that this tree and the editor
# tree build against the same synthrt commit. A fetch by commit requires no archive hash.
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/diffscope/synthrt.git
    REF f2f0f8ee3669206ed90f951c17c397a23c5e4b6d
    HEAD_REF onnxruntime-builds-uptake
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        onnx WITH_ONNX
)

# dsinfer is disabled unless the onnx feature is selected: the linguist domain of wolf requires
# neither dsinfer nor the ONNX driver, because the inference and singer categories are part of
# synthrt. The onnx feature enables dsinfer for the model-backed G2P variant.
#
# No ONNX Runtime files are staged. synthrt locates the onnxruntime-builds package itself, and the
# dependency of the onnx feature has already installed that package into the same tree, so its
# headers are on the find_package search path.
set(_synthrt_dsinfer OFF)

if(WITH_ONNX)
    set(_synthrt_dsinfer ON)
endif()

# DirectML is a Windows API. On other platforms dsinfer runs the CPU provider, so the option is
# enabled only on Windows instead of being passed to dsinfer to ignore.
set(_synthrt_directml OFF)

if(VCPKG_TARGET_IS_WINDOWS)
    set(_synthrt_directml ON)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DSYNTHRT_BUILD_TESTS:BOOL=OFF
        -DSYNTHRT_BUILD_DSINFER:BOOL=${_synthrt_dsinfer}
        # Enabled by default upstream. This port builds no CUDA provider: Windows runs the DirectML
        # provider and every other platform runs the CPU provider.
        -DDSINFER_ENABLE_CUDA:BOOL=OFF
        -DDSINFER_ENABLE_DIRECTML:BOOL=${_synthrt_directml}
)

vcpkg_cmake_install()

# synthrt installs two CMake packages side by side under lib/cmake: synthrt itself and, with the
# onnx feature, dsinfer. The fixup of one package must not delete the parent directory; otherwise
# the other package is silently lost, and consumers must locate the dsinfer library manually.
vcpkg_cmake_config_fixup(
    PACKAGE_NAME synthrt
    CONFIG_PATH lib/cmake/synthrt
    DO_NOT_DELETE_PARENT_CONFIG_PATH
)
if(WITH_ONNX)
    vcpkg_cmake_config_fixup(
        PACKAGE_NAME dsinfer
        CONFIG_PATH lib/cmake/dsinfer
        DO_NOT_DELETE_PARENT_CONFIG_PATH
    )
endif()
file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/lib/cmake"
    "${CURRENT_PACKAGES_DIR}/debug/lib/cmake"
)

# The ONNX driver is not a link target. Like every driver, it is loaded at run time as a plugin
# found on a search path, under lib/plugins/dsinfer/inferencedrivers. On Windows the plugins install
# their DLLs there, which the vcpkg layout check would otherwise reject.
set(VCPKG_POLICY_ALLOW_DLLS_IN_LIB enabled)

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

vcpkg_copy_pdbs()
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
