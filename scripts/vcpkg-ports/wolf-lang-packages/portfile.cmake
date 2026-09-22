# Data only: nothing is compiled. Each selected feature names one archive from a wolf release,
# which is downloaded, checked against its SHA512, and unpacked into a Package search directory.
#
# The release has to be reachable without credentials. This port deliberately carries no token
# handling: a credential belongs in neither a committed file nor the environment of every consumer
# that builds it, and a data-only port whose point is that anyone can install it has no business
# holding one. While the release is private, use the consumer side escape hatch instead, which is
# the WOLF_LANG_PACKAGES_SOURCE cache variable pointing at an unpacked local copy.
#
# The archives unpack to directories rather than to single files because the loader in synthrt
# main accepts directories only.

set(VCPKG_POLICY_EMPTY_PACKAGE enabled)

include("${CMAKE_CURRENT_LIST_DIR}/assets.cmake")

set(WOLF_LANG_RELEASE_TAG "lang-v${WOLF_LANG_PACKAGES_BUNDLE_VERSION}")
set(WOLF_LANG_BASE_URL
    "https://github.com/diffscope/wolf/releases/download/${WOLF_LANG_RELEASE_TAG}")

set(_install_root "${CURRENT_PACKAGES_DIR}/share/wolf/packages")
file(MAKE_DIRECTORY "${_install_root}")

set(_installed "")
foreach(_suite IN LISTS WOLF_LANG_PACKAGES_SUITES)
    if(NOT _suite IN_LIST FEATURES)
        continue()
    endif()

    string(TOUPPER "${_suite}" _key)
    string(REPLACE "-" "_" _key "${_key}")
    if(NOT DEFINED WOLF_LANG_${_key}_FILE)
        message(FATAL_ERROR "wolf-lang-packages: no asset recorded for feature ${_suite}")
    endif()

    vcpkg_download_distfile(_archive
        URLS "${WOLF_LANG_BASE_URL}/${WOLF_LANG_${_key}_FILE}"
        FILENAME "${WOLF_LANG_${_key}_FILE}"
        SHA512 "${WOLF_LANG_${_key}_SHA512}"
    )
    vcpkg_extract_source_archive(_extracted ARCHIVE "${_archive}" NO_REMOVE_ONE_LEVEL)
    file(COPY "${_extracted}/${WOLF_LANG_${_key}_DIR}" DESTINATION "${_install_root}")
    list(APPEND _installed "${_suite}")
endforeach()

set(WOLF_LANG_PACKAGES_INSTALLED "${_installed}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/wolf-lang-packages-config.cmake.in"
    "${CURRENT_PACKAGES_DIR}/share/${PORT}/wolf-lang-packages-config.cmake" @ONLY)

# Written directly rather than through vcpkg_install_copyright, which requires FILE_LIST to name
# at least one file. There is no one file to name: which licences apply depends on which features
# were selected, and several packages carry none because upstream shipped none. Collecting whatever
# the installed packages do carry says more than a fixed list could.
string(REPLACE ";" ", " _installed_text "${_installed}")
set(_copyright "Language resources redistributed by the wolf project.\n")
string(APPEND _copyright "Installed packages: ${_installed_text}\n\n")
string(APPEND _copyright
       "Individual dictionaries carry their own license files where upstream supplied them.\n"
       "Those files are installed alongside the data and reproduced below.\n")
file(GLOB_RECURSE _licenses "${_install_root}/*/License.txt" "${_install_root}/*/LICENSE"
     "${_install_root}/*/COPYING")
foreach(_license IN LISTS _licenses)
    file(RELATIVE_PATH _where "${_install_root}" "${_license}")
    file(READ "${_license}" _text)
    string(APPEND _copyright "\n---- ${_where} ----\n${_text}\n")
endforeach()
file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright" ${_copyright})
