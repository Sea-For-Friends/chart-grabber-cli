# Datum, projection types, projection sets, local coord converter, and the geometry, point
# streams, WKB, tile iterators, GeoCalc, coordinate formatting and the PROJ adapter below.
add_library(cg_geo STATIC
  geo_types.cpp
  log_geo.cpp
  datum.cpp
  projection_type.cpp
  projection_type_factory.cpp
  projection.cpp
  projection_set.cpp
  local_coord_converter.cpp
  convert_projection.cpp
  tile_rect.cpp
  # LonLat geometry hierarchy, its factory, WKB and geometry_func.
  geometry_lonlat.cpp
  geometry_factory.cpp
  wkb.cpp
  geometry_func.cpp
  # The point-stream enumerator pipeline.
  point_stream.cpp
  point_stream_clip.cpp
  # The projected geometry mirror.
  geometry_projected.cpp
  # The projected planar predicates. The projected factory and provider are in the already-listed
  # geometry_factory.cpp.
  projected_calc.cpp
  # The PROJ 9 adapter, custom CRS / datum-shift display.
  proj/proj_converter.cpp
  proj/proj_data_dir.cpp
  # Coordinate representation enums/settings, degrToStr formatting.
  coord_representation.cpp
  coord_to_string.cpp
  # The geographic/UTM-GK/MGRS text scanner.
  coord_from_string.cpp
  # The CRS transformer (UTM/UPS/Gauss-Kruger/MGRS/SK-42/GSK-2011), wired into coord_to_string.cpp
  # and coord_from_string.cpp above.
  coord_transformer.cpp
  # distance/area/speed/altitude unit formatting.
  value_to_string.cpp
  # The three tile iterators (tile_rect.cpp is listed above).
  tile_iterator.cpp
  # Tile counting/splitting, the iterator factory + data provider, and GeoCalc.
  tile_count.cpp
  geo_calc.cpp
  config_provider_helpers.cpp
)
target_include_directories(cg_geo PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cg_geo
  # geodesic is an OBJECT library: CMake only forwards an OBJECT library's compiled objects onto a
  # final executable's link line when the dependency is PUBLIC/INTERFACE all the way through: PRIVATE
  # here builds cg_geo fine but drops geod_init/geod_inverse/geod_direct from anything linking
  # against libcg_geo.a (e.g. geo_test), unlike a PRIVATE STATIC dependency (cityhash in cg_core),
  # which CMake still propagates transitively for static-to-static linking.
  PUBLIC  cg_core Qt6::Core geodesic
  PRIVATE cg_warnings
)

# PROJ >= 9 is optional. Absent, cg_geo still builds -- proj_converter.cpp compiles its
# PROJ-less stub (CG_HAVE_PROJ unset -> 0) and custom-CRS display is disabled.
find_package(PROJ 9 QUIET)
if (NOT PROJ_FOUND)
  # Debian 12's PROJ CMake package refuses a cross build (its version file records cross-compiling = FALSE) while proj.pc has no such check:
  # fall back to pkg-config only when the CMake package was not found, so a native build resolves PROJ exactly as before.
  find_package(PkgConfig QUIET)
  if (PkgConfig_FOUND)
    pkg_check_modules(CG_PROJ QUIET IMPORTED_TARGET proj>=9)
  endif()
endif()
if (PROJ_FOUND)
  target_link_libraries(cg_geo PRIVATE PROJ::proj)
  target_compile_definitions(cg_geo PRIVATE CG_HAVE_PROJ=1)
elseif (CG_PROJ_FOUND)
  target_link_libraries(cg_geo PRIVATE PkgConfig::CG_PROJ)
  target_compile_definitions(cg_geo PRIVATE CG_HAVE_PROJ=1)
else()
  message(WARNING "PROJ >= 9 not found: custom CRS, UTM/MGRS/SK-42/GSK-2011 display disabled")
endif()
