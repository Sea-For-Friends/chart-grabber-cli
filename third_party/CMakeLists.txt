# Vendored Google CityHash 1.1.1 (city.h, city.cc; citycrc.h not needed).
# Upstream: https://github.com/google/cityhash (MIT). Vendored because the legacy app linked
# CityHash.dll and tile content hashes must stay byte-identical (survey 03).
# See third_party/cityhash/README.md for the version evidence and the single local modification.
# Deliberately NOT linked against cg_warnings: upstream code does not build under
# -Werror -Wold-style-cast and must not be modified.
add_library(cityhash STATIC cityhash/city.cc)
# SYSTEM: upstream headers must not be linted or warned about (they are not ours to fix).
target_include_directories(cityhash SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(cityhash PUBLIC cxx_std_20)
set_target_properties(cityhash PROPERTIES POSITION_INDEPENDENT_CODE ON)

# T-0016 (cg_geo): vendored GeographicLib geodesic routines, see third_party/geodesic/README.md.
# Guarded: geodesic/ lands with T-0016 and may not exist yet in every checkout of this history.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/geodesic/CMakeLists.txt")
  add_subdirectory(geodesic)
endif()
