# T-0184: tests of the app module (docs/specs/app-cli.md). Later app tickets append their test files here.
set(CG_APP_TEST_SRCS
  proj_converter_factory_adapter_test.cpp # T-0184
  app_context_test.cpp # T-0184
  tile_estimate_test.cpp # T-0190
  fill_report_test.cpp # T-0190
  job_test.cpp # T-0185
  console_test.cpp # T-0186
  gpx_route_reader_test.cpp # T-0187
  export_runner_test.cpp # T-0192
  download_runner_test.cpp # T-0191
  job_area_test.cpp # T-0188
  job_checks_test.cpp # T-0189
  http_test_server_shared.cpp # T-0193
  cli_main_test.cpp # T-0193
  cli_end_to_end_test.cpp # T-0193
  probe_test.cpp # T-0194
  machine_output_test.cpp # T-0202
  snapshot_test.cpp # T-0242
  merged_zmp_info_set_test.cpp # T-0239
  cli_snapshot_test.cpp # T-0243
  snapshot_resolver_test.cpp # T-0243
  snapshot_prune_test.cpp # T-0246
  cli_source_json_test.cpp # T-0224
  cli_js_script_verdict_test.cpp # T-0366
)
cg_add_test(app_test ${CG_APP_TEST_SRCS})
target_link_libraries(app_test PRIVATE cg_app cg_region cg_maptype cg_bitmap cg_vector cg_storage cg_net cg_script cg_geo cg_core Qt6::Core Qt6::Gui)
target_include_directories(app_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# T-0335: without it the tests walk up from the working directory (the nocmake gate runs in the tree; ctest runs in the build dir)
target_compile_definitions(app_test PRIVATE
  CG_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
