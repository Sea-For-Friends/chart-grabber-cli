# region module. Files are added as the module grows.
add_library(cg_region STATIC
  log_region.cpp
  zoom_array_func.cpp
  region_process_progress_info.cpp
  region_process_task_abstract.cpp
  storage_export_to_mbtiles.cpp
  download_session.cpp
  download_session_save_load.cpp
  download_task_provider.cpp
  region_process_progress_info_download.cpp
  thread_download_tiles.cpp
  export_task_abstract.cpp
  export_task_to_mbtiles.cpp
  progress_download.cpp
  tiles_download.cpp
  provider_tiles_download.cpp
  export_mbtiles.cpp
  export_provider_mbtiles.cpp
)
target_include_directories(cg_region PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cg_region
  PUBLIC  cg_core cg_geo cg_storage cg_net cg_maptype Qt6::Core
  PRIVATE cg_warnings
)
