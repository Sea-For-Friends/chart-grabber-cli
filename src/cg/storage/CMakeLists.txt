# Value types every backend disagrees about (tile date, map version, abilities, tile info/rect
# info) plus the Src/Predicate aliases. The SQLite3/LibArchive dependencies are added with the
# sources that need them.
add_library(cg_storage STATIC
  map_version.cpp
  predicates.cpp
  tile_date.cpp
  tile_name_codec.cpp
  tile_rect_info.cpp
  tile_storage_abilities.cpp
  codecs/es_tile_name_codec.cpp
  codecs/gmv_tile_name_codec.cpp
  codecs/sas_tile_name_codec.cpp
  codecs/gm1_tile_name_codec.cpp
  codecs/gm2_tile_name_codec.cpp
  codecs/gm3_tile_name_codec.cpp
  codecs/mobac_tile_name_codec.cpp
  codecs/osmand_tile_name_codec.cpp
  codecs/tms_tile_name_codec.cpp
  codecs/region_tile_name_codec.cpp
  storage_state.cpp
  storage_state_proxy.cpp
  tile_storage_of_map_type.cpp
  tile_pyramid_notifier.cpp
  tile_storage_base.cpp
  cache_type.cpp
  global_cache_config.cpp
  tile_storage_type.cpp
  tile_info_mem_cache.cpp
  simple_tile_storage_config.cpp
  file_system_tile_storage.cpp
  file_system_tile_storage_type.cpp
  ram_tile_storage.cpp
  ram_tile_storage_type.cpp
  sqlite/sqlite_db.cpp         # The raw sqlite3 RAII wrapper
  sqlite/sqlite_error_log.cpp
  sqlite/sqlite_file_connection.cpp
  sqlite/sqlite_file_connection_pool.cpp
  sqlite/mbtiles_connection.cpp
  sqlite/rmaps_connection.cpp
  sqlite/oruxmaps_connection.cpp
  sqlite/sqlite_file_tile_storage.cpp
  sqlite/sqlite_file_tile_storage_type.cpp
  sqlite/sqlite_file_detect.cpp # The single-file format sniffer, content type, EPSG
  sqlite/sqlite_region_sql.cpp # Region-cache SQL text builders (no sqlite3 include)
  sqlite/sqlite_region_schema.cpp   # The holder port (override files, content-type/version mapping, error log)
  sqlite/sqlite_region_handler.cpp  # Region-file handler ctor + column discovery only
  sqlite/sqlite_region_handler_read.cpp  # tileInfo/listTileVersions/listVersions
  sqlite/sqlite_region_handler_write.cpp  # saveTile / deleteTile / tileRectInfo
  sqlite/sqlite_region_pool.cpp  # Open region-file map, per-file mutex, idle sweep
  sqlite/sqlite_region_scan.cpp  # Scan enumerator
  sqlite/sqlite_region_tile_storage.cpp  # The ITileStorage face of cache_sqlite
  sqlite/sqlite_region_tile_storage_type.cpp
  cache_converter.cpp
  archive_tile_storage.cpp     # libarchive-backed tar/zip sequential storage
  tile_storage_type_list.cpp
  tile_storage_type_list_simple.cpp
)
find_package(SQLite3 REQUIRED)  # System sqlite3 only
find_package(LibArchive REQUIRED)

# BerkeleyDB removed: no libdb, no zlib in this module; snapshot: tag archive/with-berkeleydb.
target_include_directories(cg_storage PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cg_storage
  PUBLIC  Qt6::Core cg_core cg_geo
  PRIVATE cg_warnings SQLite::SQLite3 LibArchive::LibArchive
  # Tile content-type magic-byte sniffing uses cg::core::detectContentType
  # (content_detector.{h,cpp} lives in cg_core), so no extra link edge is needed here -- see
  # sqlite_file_connection.cpp's tileContentType().
)
