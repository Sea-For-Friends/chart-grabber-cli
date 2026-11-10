# Tests of the app module. Test files are added here as the module grows.
set(CG_APP_TEST_SRCS
  proj_converter_factory_adapter_test.cpp
  app_context_test.cpp
  tile_estimate_test.cpp
  fill_report_test.cpp
  job_test.cpp
  console_test.cpp
  gpx_route_reader_test.cpp
  export_runner_test.cpp
  download_runner_test.cpp
  job_area_test.cpp
  job_checks_test.cpp
  http_test_server_shared.cpp
  cli_main_test.cpp
  cli_end_to_end_test.cpp
  probe_test.cpp
  machine_output_test.cpp
  snapshot_test.cpp
  merged_zmp_info_set_test.cpp
  cli_snapshot_test.cpp
  snapshot_resolver_test.cpp
  snapshot_prune_test.cpp
  cli_source_json_test.cpp
  cli_js_script_verdict_test.cpp
)
cg_add_test(app_test ${CG_APP_TEST_SRCS})
target_link_libraries(app_test PRIVATE cg_app cg_region cg_maptype cg_bitmap cg_vector cg_storage cg_net cg_script cg_geo cg_core Qt6::Core Qt6::Gui)
target_include_directories(app_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# Without it the tests walk up from the working directory (the nocmake gate runs in the tree; ctest runs in the build dir)
target_compile_definitions(app_test PRIVATE
  CG_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
