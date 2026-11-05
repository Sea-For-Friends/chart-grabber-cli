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

# Owner 2026-11-05: no documentation in the packages (it is on GitHub); the .deb carries copyright + README (cmake/cli_package.cmake).
