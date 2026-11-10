set(CG_REGION_TEST_SRCS
  zoom_array_func_test.cpp
  region_process_progress_info_test.cpp
  region_process_task_abstract_test.cpp
  storage_export_to_mbtiles_classic_test.cpp
  download_session_test.cpp
  download_session_sls_test.cpp
  download_task_provider_test.cpp
  region_process_progress_info_download_test.cpp
  storage_export_to_mbtiles_tilemill_test.cpp
  thread_download_tiles_test.cpp
  export_task_abstract_test.cpp
  export_task_to_mbtiles_test.cpp
  progress_download_test.cpp
  tiles_download_test.cpp
  provider_tiles_download_test.cpp
  export_mbtiles_test.cpp
  export_provider_mbtiles_test.cpp
)
cg_add_test(region_test ${CG_REGION_TEST_SRCS})
target_link_libraries(region_test PRIVATE cg_region cg_maptype cg_vector cg_storage cg_geo cg_core Qt6::Core Qt6::Gui)
target_include_directories(region_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(region_test PRIVATE
  CG_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
