# Tests of the storage module; fixtures come from tools/gen_cache_fixtures.py and real cache
# data.
set(CG_STORAGE_TEST_SRCS
  map_version_test.cpp
  predicates_test.cpp
  tile_date_test.cpp
  tile_name_codec_test.cpp  # Reads tests/fixtures/cache/expected.json
  tile_rect_info_test.cpp
  tile_storage_abilities_test.cpp
  region_tile_name_codec_test.cpp
  cache_type_test.cpp
  storage_state_test.cpp
  storage_state_proxy_test.cpp
  tile_storage_of_map_type_test.cpp
  tile_pyramid_notifier_test.cpp
  global_cache_config_test.cpp
  tile_storage_type_test.cpp
  tile_info_mem_cache_test.cpp
  simple_tile_storage_config_test.cpp
  file_system_tile_storage_test.cpp
  ram_tile_storage_test.cpp
  sqlite_db_test.cpp
  sqlite_file_connection_test.cpp
  mbtiles_test.cpp
  rmaps_test.cpp
  oruxmaps_test.cpp
  sqlite_file_detect_test.cpp
  sqlite_region_sql_test.cpp
  sqlite_region_handler_test.cpp
  sqlite_region_handler_read_test.cpp
  sqlite_region_handler_write_test.cpp
  sqlite_region_pool_test.cpp
  sqlite_region_tile_storage_test.cpp
  cache_converter_test.cpp
  archive_tile_storage_test.cpp
  sqlite_file_tile_storage_test.cpp
  tile_storage_type_list_test.cpp
)
cg_add_test(storage_test ${CG_STORAGE_TEST_SRCS})
target_link_libraries(storage_test PRIVATE cg_storage cg_core Qt6::Core)
target_compile_definitions(storage_test PRIVATE
  CG_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
