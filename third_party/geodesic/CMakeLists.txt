# Vendored GeographicLib geodesic routines (geodesic.c, geodesic.h). See README.md.
# Deliberately NOT linked against cg_warnings: upstream C does not build under
# -Werror -Wpedantic -Wold-style-cast and must not be modified.
add_library(geodesic OBJECT geodesic.c)
# Parent dir (not this one): callers write #include <geodesic/geodesic.h>, matching the
# <cityhash/city.h> convention third_party/CMakeLists.txt already uses for cityhash.
target_include_directories(geodesic SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(geodesic PROPERTIES POSITION_INDEPENDENT_CODE ON)
