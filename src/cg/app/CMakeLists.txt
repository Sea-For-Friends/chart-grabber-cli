# app module: the composition root and its glue. Files are added as the app module grows.
add_library(cg_app STATIC
  log_app.cpp
  proj_converter_factory_adapter.cpp
  app_context.cpp
  tile_estimate.cpp
  fill_report.cpp
  job.cpp
  console.cpp
  gpx_route_reader.cpp
  export_runner.cpp
  download_runner.cpp
  job_area.cpp
  job_checks.cpp
  cli_main.cpp
  probe.cpp
  machine_output.cpp
  snapshot.cpp
  merged_zmp_info_set.cpp
  snapshot_resolver.cpp
  snapshot_store.cpp
  snapshot_prune.cpp
)
target_include_directories(cg_app PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cg_app
  PUBLIC  cg_region cg_maptype cg_bitmap cg_vector cg_storage cg_net cg_script cg_geo cg_core Qt6::Core Qt6::Gui
  PRIVATE cg_warnings
)
