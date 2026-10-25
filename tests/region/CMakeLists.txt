set(CG_REGION_TEST_SRCS
  zoom_array_func_test.cpp # T-0157
  region_process_progress_info_test.cpp # T-0169
  region_process_task_abstract_test.cpp # T-0169
  storage_export_to_mbtiles_classic_test.cpp # T-0170
  download_session_test.cpp # T-0164
  download_session_sls_test.cpp # T-0165
  download_task_provider_test.cpp # T-0166
  region_process_progress_info_download_test.cpp # T-0167
  storage_export_to_mbtiles_tilemill_test.cpp # T-0171
  thread_download_tiles_test.cpp # T-0168
  export_task_abstract_test.cpp # T-0172
  export_task_to_mbtiles_test.cpp # T-0172
  progress_download_test.cpp # T-0182
  tiles_download_test.cpp # T-0181
  provider_tiles_download_test.cpp # T-0181
  export_mbtiles_test.cpp  # T-0183
  export_provider_mbtiles_test.cpp  # T-0183
)
cg_add_test(region_test ${CG_REGION_TEST_SRCS})
target_link_libraries(region_test PRIVATE cg_region cg_maptype cg_vector cg_storage cg_geo cg_core Qt6::Core Qt6::Gui)
target_include_directories(region_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(region_test PRIVATE
  CG_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
