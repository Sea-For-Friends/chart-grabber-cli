# region module (docs/specs/region-download.md §1.5, docs/specs/region-export.md). Files are added as the region tickets land.
add_library(cg_region STATIC
  log_region.cpp
  zoom_array_func.cpp # T-0157
  region_process_progress_info.cpp # T-0169
  region_process_task_abstract.cpp # T-0169
  storage_export_to_mbtiles.cpp # T-0170
  download_session.cpp # T-0164
  download_session_save_load.cpp # T-0164
  download_task_provider.cpp # T-0166
  region_process_progress_info_download.cpp # T-0167
  thread_download_tiles.cpp # T-0168
  export_task_abstract.cpp # T-0172
  export_task_to_mbtiles.cpp # T-0172
  progress_download.cpp # T-0182
  tiles_download.cpp # T-0181
  provider_tiles_download.cpp # T-0181
  export_mbtiles.cpp # T-0183
  export_provider_mbtiles.cpp # T-0183
)
target_include_directories(cg_region PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cg_region
  PUBLIC  cg_core cg_geo cg_storage cg_net cg_maptype Qt6::Core
  PRIVATE cg_warnings
)
