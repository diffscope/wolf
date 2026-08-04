# ----------------------------------
# Project Constants
# ----------------------------------
set(WOLF_INCLUDE_DIR "../include")

# Install the CMake package files and the public headers alongside the binaries.
# The generic helpers gate both on <proj>_DEVEL, which defaults to off.
set(WOLF_DEVEL ON)

# Windows resource metadata.
set(WOLF_RC_DESCRIPTION "${PROJECT_DESCRIPTION}")
set(WOLF_RC_COPYRIGHT "Copyright (c) 2023-present Team OpenVPI")

function(_wolf_common_configure_target _target)
    if(WIN32)
        qm_add_win_rc(${_target}
            NAME ${WOLF_INSTALL_NAME}
            DESCRIPTION "${WOLF_RC_DESCRIPTION}"
            COPYRIGHT "${WOLF_RC_COPYRIGHT}"
        )
    endif()

    wolf_set_default_install_rpath(${_target})
endfunction()

set(WOLF_POST_CONFIGURE_COMMANDS _wolf_common_configure_target)

# ----------------------------------
# Include Build Helpers
# ----------------------------------
qm_import(private/BuildSystem)
qm_setup_build_repo_helpers()
