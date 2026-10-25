# T-0021A (storage slice A): value types every backend disagrees about (tile date, map version,
# abilities, tile info/rect info) plus the Src/Predicate aliases. No backend, no I/O -- the
# SQLite3/LibArchive dependencies from storage.md §9.1 land with the tickets that need
# them (T-0021B..E, T-0022..T-0024).
add_library(cg_storage STATIC
  map_version.cpp
  predicates.cpp
  tile_date.cpp
  tile_name_codec.cpp
  tile_rect_info.cpp
  tile_storage_abilities.cpp
  codecs/es_tile_name_codec.cpp     # T-0021B
  codecs/gmv_tile_name_codec.cpp    # T-0021B
  codecs/sas_tile_name_codec.cpp    # T-0021B
  codecs/gm1_tile_name_codec.cpp    # T-0021C
  codecs/gm2_tile_name_codec.cpp    # T-0021C
  codecs/gm3_tile_name_codec.cpp    # T-0021C
  codecs/mobac_tile_name_codec.cpp  # T-0021C
  codecs/osmand_tile_name_codec.cpp # T-0021C
  codecs/tms_tile_name_codec.cpp    # T-0021C
  codecs/region_tile_name_codec.cpp # T-0021C
  storage_state.cpp          # T-0021D
  storage_state_proxy.cpp    # T-0136
  tile_storage_of_map_type.cpp  # T-0138
  tile_pyramid_notifier.cpp  # T-0021D
  tile_storage_base.cpp      # T-0021D
  cache_type.cpp             # T-0021D
  global_cache_config.cpp    # T-0021D
  tile_storage_type.cpp      # T-0021D
  tile_info_mem_cache.cpp    # T-0021E1
  simple_tile_storage_config.cpp    # T-0021E2
  file_system_tile_storage.cpp      # T-0021F1
  file_system_tile_storage_type.cpp # T-0021F3b
  ram_tile_storage.cpp              # T-0021F3a
  ram_tile_storage_type.cpp         # T-0021F3b
  sqlite/sqlite_db.cpp         # T-0022A: the raw sqlite3 RAII wrapper (ADR-0005 decision 2)
  sqlite/sqlite_error_log.cpp  # T-0022A
  sqlite/sqlite_file_connection.cpp       # T-0022C1
  sqlite/sqlite_file_connection_pool.cpp  # T-0022C1
  sqlite/mbtiles_connection.cpp # T-0022C2
  sqlite/rmaps_connection.cpp # T-0022C3
  sqlite/oruxmaps_connection.cpp # T-0022D1
  sqlite/sqlite_file_tile_storage.cpp # T-0022C4
  sqlite/sqlite_file_tile_storage_type.cpp # T-0022C4
  sqlite/sqlite_file_detect.cpp # T-0022D2: the single-file format sniffer, content type, EPSG
  sqlite/sqlite_region_sql.cpp # T-0022B1: region-cache SQL text builders (no sqlite3 include)
  sqlite/sqlite_region_schema.cpp   # T-0022B2: the holder port (override files, content-type/version mapping, error log)
  sqlite/sqlite_region_handler.cpp  # T-0022B2: region-file handler ctor + column discovery only
  sqlite/sqlite_region_handler_read.cpp  # T-0022B3: tileInfo/listTileVersions/listVersions
  sqlite/sqlite_region_handler_write.cpp  # T-0022B4: saveTile / deleteTile / tileRectInfo
  sqlite/sqlite_region_pool.cpp  # T-0022B5: open region-file map, per-file mutex, idle sweep
  sqlite/sqlite_region_scan.cpp  # T-0022B5: scan enumerator
  sqlite/sqlite_region_tile_storage.cpp  # T-0022B6: the ITileStorage face of cache_sqlite
  sqlite/sqlite_region_tile_storage_type.cpp  # T-0022B6
  cache_converter.cpp          # T-0027
  archive_tile_storage.cpp     # T-0024: libarchive-backed tar/zip sequential storage
  tile_storage_type_list.cpp  # T-0046
  tile_storage_type_list_simple.cpp  # T-0046
)
find_package(SQLite3 REQUIRED)  # T-0022A: docs/specs/storage.md §2.15, system sqlite3 only
find_package(LibArchive REQUIRED)  # T-0024: docs/specs/storage.md §2.19, ADR-0005 decision 4

# T-0333: BerkeleyDB removed (owner 2026-10-21): no libdb, no zlib in this module; snapshot: tag archive/with-berkeleydb.
target_include_directories(cg_storage PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cg_storage
  PUBLIC  Qt6::Core cg_core cg_geo
  PRIVATE cg_warnings SQLite::SQLite3 LibArchive::LibArchive
  # T-0022C2: tile content-type magic-byte sniffing uses cg::core::detectContentType
  # (content_detector.{h,cpp} already lives in cg_core, moved there from cg_net by T-0033), so
  # no extra link edge is needed here -- see sqlite_file_connection.cpp's tileContentType().
)
