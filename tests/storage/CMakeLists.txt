# T-0021A (storage slice A). Fixture generation (storage.md §9.2) lands with the tickets that
# read real cache data (T-0021B..E, T-0022..T-0024); this slice's tests are pure value-type math
# and need no fixtures.
set(CG_STORAGE_TEST_SRCS
  map_version_test.cpp
  predicates_test.cpp
  tile_date_test.cpp
  tile_name_codec_test.cpp  # T-0021B/T-0021C -- reads tests/fixtures/cache/expected.json
  tile_rect_info_test.cpp
  tile_storage_abilities_test.cpp
  region_tile_name_codec_test.cpp  # T-0021C
  cache_type_test.cpp              # T-0021D
  storage_state_test.cpp           # T-0021D
  storage_state_proxy_test.cpp     # T-0136
  tile_storage_of_map_type_test.cpp  # T-0138
  tile_pyramid_notifier_test.cpp   # T-0021D
  global_cache_config_test.cpp     # T-0021D
  tile_storage_type_test.cpp       # T-0021D
  tile_info_mem_cache_test.cpp     # T-0021E1
  simple_tile_storage_config_test.cpp  # T-0021E2
  file_system_tile_storage_test.cpp    # T-0021F1
  ram_tile_storage_test.cpp            # T-0021F3a
  sqlite_db_test.cpp  # T-0022A
  sqlite_file_connection_test.cpp  # T-0022C1
  mbtiles_test.cpp  # T-0022C2
  rmaps_test.cpp  # T-0022C3
  oruxmaps_test.cpp  # T-0022D1
  sqlite_file_detect_test.cpp  # T-0022D2
  sqlite_region_sql_test.cpp  # T-0022B1
  sqlite_region_handler_test.cpp  # T-0022B2
  sqlite_region_handler_read_test.cpp  # T-0022B3
  sqlite_region_handler_write_test.cpp  # T-0022B4
  sqlite_region_pool_test.cpp  # T-0022B5
  sqlite_region_tile_storage_test.cpp  # T-0022B6
  cache_converter_test.cpp  # T-0027
  archive_tile_storage_test.cpp  # T-0024
  sqlite_file_tile_storage_test.cpp  # T-0022C4
  tile_storage_type_list_test.cpp    # T-0046
)
cg_add_test(storage_test ${CG_STORAGE_TEST_SRCS})
target_link_libraries(storage_test PRIVATE cg_storage cg_core Qt6::Core)
target_compile_definitions(storage_test PRIVATE  # T-0021B
  CG_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
