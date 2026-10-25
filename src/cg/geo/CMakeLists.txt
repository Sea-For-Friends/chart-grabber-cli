# T-0016 (geo slice A): datum, projection types, projection sets, local coord converter.
# Remaining sources of docs/specs/geo.md §12.2 (geometry, point streams, wkb, tile iterators,
# GeoCalc, coord formatting, PROJ adapter) land with T-0011..T-0014.
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
  # T-0017A (geo slice B1): LonLat geometry hierarchy, its factory, WKB and geometry_func.
  geometry_lonlat.cpp
  geometry_factory.cpp
  wkb.cpp
  geometry_func.cpp
  # T-0017B (geo slice B2): the point-stream enumerator pipeline.
  point_stream.cpp
  point_stream_clip.cpp
  # T-0017C1 (geo slice B3, split C1): the projected geometry mirror.
  geometry_projected.cpp
  # T-0017C2 (geo slice B3, split C2): the projected planar predicates. The projected factory and
  # provider (§3.5.1-§3.5.3) land in the already-listed geometry_factory.cpp.
  projected_calc.cpp
  # T-0020 (geo slice E): the PROJ 9 adapter, custom CRS / datum-shift display.
  proj/proj_converter.cpp
  # T-0019A1 (geo slice D1): coordinate representation enums/settings, degrToStr formatting.
  coord_representation.cpp
  coord_to_string.cpp
  # T-0019B1 (geo slice D2, split B1): the geographic/UTM-GK/MGRS text scanner.
  coord_from_string.cpp
  # T-0019B2 (geo slice D2, split B2): the CRS transformer (UTM/UPS/Gauss-Kruger/MGRS/SK-42/
  # GSK-2011), wired into coord_to_string.cpp and coord_from_string.cpp above.
  coord_transformer.cpp
  # T-0019A2 (geo slice D1, part 2): distance/area/speed/altitude unit formatting.
  value_to_string.cpp
  # T-0018A (geo slice C split A): the three tile iterators (tile_rect.cpp landed with T-0016).
  tile_iterator.cpp
  # T-0018B (geo slice C split B): tile counting/splitting, the iterator factory + data provider,
  # and GeoCalc.
  tile_count.cpp
  geo_calc.cpp
  config_provider_helpers.cpp # T-0158
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

# T-0020: PROJ >= 9 is optional. Absent, cg_geo still builds -- proj_converter.cpp compiles its
# PROJ-less stub (CG_HAVE_PROJ unset -> 0) and custom-CRS display is disabled (docs/specs/geo.md
# §12.2, ADR-0006 decision 3).
find_package(PROJ 9 QUIET)
if (PROJ_FOUND)
  target_link_libraries(cg_geo PRIVATE PROJ::proj)
  target_compile_definitions(cg_geo PRIVATE CG_HAVE_PROJ=1)
else()
  message(WARNING "PROJ >= 9 not found: custom CRS, UTM/MGRS/SK-42/GSK-2011 display disabled")
endif()
