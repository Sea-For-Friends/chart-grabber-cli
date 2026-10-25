# T-0204: Debian package of the CLI (stage 1, Debian 13 / Pi OS trixie). Built by tools/build_deb.sh with `cpack -G DEB`.
# Only the `cli` install component is packaged, so nothing a third-party subdirectory installs ends up in the .deb.
set(CPACK_GENERATOR DEB)
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_ALL cli)
set(CPACK_PACKAGE_NAME chart-grabber-cli)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Command-line map tile downloader")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
# T-0324: the Maintainer field comes from a cache variable (pass -DCG_DEB_MAINTAINER="Name <address>", or the env var of the same
# name to tools/build_deb.sh); the committed default is a placeholder until the owner gives the public address.
# T-0370: the default comes from tools/public-identity.conf (the one place of the public identity).
set(_cg_maint "Chart Grabber maintainers <LOGIN@users.noreply.github.com>")
set(CG_PUBLIC_REPO_URL "<public repository URL>")  # the Source field of the Debian copyright file (cmake/debian-copyright.in)
set(_cg_identity_conf "${CMAKE_SOURCE_DIR}/tools/public-identity.conf")
if(EXISTS "${_cg_identity_conf}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_cg_identity_conf}")
  file(STRINGS "${_cg_identity_conf}" _cg_maint_line REGEX "^MAINTAINER=\".*\"$")
  if(_cg_maint_line)
    string(REGEX REPLACE "^MAINTAINER=\"(.*)\"$" "\\1" _cg_maint "${_cg_maint_line}")
  endif()
  file(STRINGS "${_cg_identity_conf}" _cg_url_line REGEX "^PUBLIC_REPO_URL=\".*\"$")
  if(_cg_url_line)
    string(REGEX REPLACE "^PUBLIC_REPO_URL=\"(.*)\"$" "\\1" CG_PUBLIC_REPO_URL "${_cg_url_line}")
  endif()
endif()
set(CG_DEB_MAINTAINER "${_cg_maint}" CACHE STRING "Maintainer field of the .deb")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CG_DEB_MAINTAINER}")
# libqt6gui6 ships platforms/libqoffscreen.so (dpkg -S in the arm64 VM); the CLI runs without a display.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6gui6")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
# Per-component copies: with component install CPack reads these before the global ones.
set(CPACK_DEBIAN_CLI_PACKAGE_NAME chart-grabber-cli)
set(CPACK_DEBIAN_CLI_PACKAGE_DEPENDS "libqt6gui6")
set(CPACK_DEBIAN_CLI_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_CLI_FILE_NAME DEB-DEFAULT)
# T-0228: licence. LICENSE (GPL-3.0-or-later, owner ruling 2026-10-15) is the package licence file; the machine-readable
# Debian copyright is installed as <doc dir>/copyright. The map-source stanza path is relative to /usr (CPACK_PACKAGING_INSTALL_PREFIX).
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
include(GNUInstallDirs)  # the root scope has no CMAKE_INSTALL_DATADIR (cli/ includes it in its own scope)
set(CG_DEB_PACKAGE_NAME chart-grabber-cli)
configure_file(${CMAKE_CURRENT_LIST_DIR}/debian-copyright.in ${CMAKE_BINARY_DIR}/debian-copyright @ONLY)
install(FILES ${CMAKE_BINARY_DIR}/debian-copyright DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/${CG_DEB_PACKAGE_NAME}
  RENAME copyright COMPONENT cli)
include(CPack)
