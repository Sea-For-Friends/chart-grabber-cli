# Tests of the geo module.
set(CG_GEO_TEST_SRCS
  datum_test.cpp
  projection_type_test.cpp
  projection_type_factory_test.cpp
  projection_test.cpp
  projection_set_test.cpp
  local_coord_converter_test.cpp
  zmp_projection_corpus_test.cpp
  geometry_lonlat_test.cpp
  wkb_test.cpp
  geometry_meta_json_test.cpp
  geometry_func_test.cpp
  point_stream_test.cpp
  geometry_projected_test.cpp
  projected_calc_test.cpp
  proj_converter_test.cpp
  coord_representation_test.cpp
  coord_to_string_test.cpp
  coord_from_string_test.cpp
  coord_transformer_test.cpp
  value_to_string_test.cpp
  # tile_rect + the three tile iterators.
  tile_iterator_test.cpp
  # Tile counting/splitting/factory/data provider + GeoCalc.
  tile_count_test.cpp
  geo_calc_test.cpp
  # TileRect::isEqual(const TileRect&)
  tile_rect_test.cpp
  # pointIsEmpty = IsNan(X) or IsNan(Y) (u_GeoFunc.pas:552)
  geo_types_test.cpp
  config_provider_helpers_test.cpp
  proj_data_dir_test.cpp
)
cg_add_test(geo_test ${CG_GEO_TEST_SRCS})
target_link_libraries(geo_test PRIVATE cg_geo cg_core Qt6::Core)
target_compile_definitions(geo_test PRIVATE
  CG_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# geo_test's own translation units (proj_converter_test.cpp, projection_type_test.cpp's
# NativeAgreesWithProjWithin1e7) need the same CG_HAVE_PROJ the library was built with -- it is a
# PRIVATE define of cg_geo and does not otherwise reach a consumer target.
find_package(PROJ 9 QUIET)
if (NOT PROJ_FOUND)   # the same pkg-config fallback as src/cg/geo/CMakeLists.txt
  find_package(PkgConfig QUIET)
  if (PkgConfig_FOUND)
    pkg_check_modules(CG_PROJ QUIET proj>=9)
  endif()
endif()
if (PROJ_FOUND OR CG_PROJ_FOUND)
  target_compile_definitions(geo_test PRIVATE CG_HAVE_PROJ=1)
endif()
