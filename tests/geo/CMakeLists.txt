# T-0016 (geo slice A). Remaining files of docs/specs/geo.md §11's 21-file plan land with
# T-0011..T-0014.
set(CG_GEO_TEST_SRCS
  datum_test.cpp
  projection_type_test.cpp
  projection_type_factory_test.cpp
  projection_test.cpp
  projection_set_test.cpp
  local_coord_converter_test.cpp
  zmp_projection_corpus_test.cpp
  # T-0017A (geo slice B1)
  geometry_lonlat_test.cpp
  wkb_test.cpp
  geometry_meta_json_test.cpp
  geometry_func_test.cpp # Rework 1: docs/handoffs/T-0017A-review.md F6
  # T-0017B (geo slice B2)
  point_stream_test.cpp
  # T-0017C1 (geo slice B3, split C1)
  geometry_projected_test.cpp
  # T-0017C2 (geo slice B3, split C2)
  projected_calc_test.cpp
  # T-0020 (geo slice E)
  proj_converter_test.cpp
  # T-0019A1 (geo slice D1)
  coord_representation_test.cpp
  coord_to_string_test.cpp
  # T-0019B1 (geo slice D2, split B1)
  coord_from_string_test.cpp
  # T-0019B2 (geo slice D2, split B2)
  coord_transformer_test.cpp
  # T-0019A2 (geo slice D1, part 2)
  value_to_string_test.cpp
  # T-0018A (geo slice C split A): tile_rect (landed with T-0016, untouched) + the three tile
  # iterators.
  tile_iterator_test.cpp
  # T-0018B (geo slice C split B): tile counting/splitting/factory/data provider + GeoCalc.
  tile_count_test.cpp
  geo_calc_test.cpp
  # T-0029: TileRect::isEqual(const TileRect&)
  tile_rect_test.cpp
  # T-0110: pointIsEmpty = IsNan(X) or IsNan(Y) (u_GeoFunc.pas:552)
  geo_types_test.cpp
  config_provider_helpers_test.cpp # T-0158
)
cg_add_test(geo_test ${CG_GEO_TEST_SRCS})
target_link_libraries(geo_test PRIVATE cg_geo cg_core Qt6::Core)
target_compile_definitions(geo_test PRIVATE
  CG_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# T-0020: geo_test's own translation units (proj_converter_test.cpp, projection_type_test.cpp's
# NativeAgreesWithProjWithin1e7) need the same CG_HAVE_PROJ the library was built with -- it is a
# PRIVATE define of cg_geo and does not otherwise reach a consumer target.
find_package(PROJ 9 QUIET)
if (PROJ_FOUND)
  target_compile_definitions(geo_test PRIVATE CG_HAVE_PROJ=1)
endif()
