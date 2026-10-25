# app module (docs/specs/app-cli.md §5.2): the composition root and its glue. Files are added as the app tickets land.
add_library(cg_app STATIC
  log_app.cpp # T-0184
  proj_converter_factory_adapter.cpp # T-0184
  app_context.cpp # T-0184
  tile_estimate.cpp # T-0190
  fill_report.cpp # T-0190
  job.cpp # T-0185
  console.cpp # T-0186
  gpx_route_reader.cpp # T-0187
  export_runner.cpp # T-0192
  download_runner.cpp # T-0191
  job_area.cpp # T-0188
  job_checks.cpp # T-0189
  cli_main.cpp # T-0193
  probe.cpp # T-0194
  machine_output.cpp # T-0202
  snapshot.cpp # T-0242
  merged_zmp_info_set.cpp # T-0239
  snapshot_resolver.cpp # T-0243
  snapshot_store.cpp # T-0243
  snapshot_prune.cpp # T-0246
)
target_include_directories(cg_app PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cg_app
  PUBLIC  cg_region cg_maptype cg_bitmap cg_vector cg_storage cg_net cg_script cg_geo cg_core Qt6::Core Qt6::Gui
  PRIVATE cg_warnings
)
