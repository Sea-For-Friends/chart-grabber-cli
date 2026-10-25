# The chart-grabber-cli executable (docs/specs/app-cli.md 2.6). Not under src/: tools/nocmake_build.sh compiles every
# src/**/*.cpp into libcg.a, and a main() there would replace the test runner.
add_executable(chart-grabber-cli main.cpp)
target_link_libraries(chart-grabber-cli PRIVATE cg_app cg_warnings)
target_compile_definitions(chart-grabber-cli PRIVATE CG_TILES_VERSION="${PROJECT_VERSION}")
# T-0200: CG_GIT_COMMIT for --version. Only this target depends on the commit (cg_app does not).
# T-0264: CHART_GRABBER_COMMIT overrides the commit (12 hex[-dirty]); empty = git, then the archive stamp (cli/git_commit.cmake).
set(CHART_GRABBER_COMMIT "" CACHE STRING "Commit printed by --version (12 lowercase hex, optional -dirty); empty = detect")
add_custom_target(cg_git_commit ALL
  COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOVERRIDE=${CHART_GRABBER_COMMIT} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/git_commit.h -P ${CMAKE_CURRENT_SOURCE_DIR}/git_commit.cmake
  BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/git_commit.h)
add_dependencies(chart-grabber-cli cg_git_commit)
target_include_directories(chart-grabber-cli PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# T-0204: install rule for the Debian package (cmake/cli_package.cmake packages only this component).
include(GNUInstallDirs)
install(TARGETS chart-grabber-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT cli)

# T-0291: no map sources are installed (ADR-0017).

# T-0301: the user guides and the changelog go to /usr/share/doc/chart-grabber-cli/ (the .deb packs the cli component).
# The changelog is installed as CHANGELOG (owner 2026-10-18); the guides keep their .md names.
install(FILES
  ${CMAKE_SOURCE_DIR}/docs/user/cli-guide.md
  ${CMAKE_SOURCE_DIR}/docs/user/sources-guide.md
  ${CMAKE_SOURCE_DIR}/docs/user/sources-tutorial.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/chart-grabber-cli COMPONENT cli)
# T-0365: the tutorial's example sources, next to the guides (the guides link flat, the examples are in examples/sources/).
install(DIRECTORY ${CMAKE_SOURCE_DIR}/docs/user/examples/sources
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/chart-grabber-cli/examples COMPONENT cli)
install(FILES ${CMAKE_SOURCE_DIR}/CHANGELOG.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/chart-grabber-cli RENAME CHANGELOG COMPONENT cli)
