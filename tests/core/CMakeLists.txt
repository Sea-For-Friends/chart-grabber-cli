# One test binary per module keeps gtest_discover_tests cheap.
qt_add_resources(CG_CORE_TEST_RES test_resources.qrc) # two dummy files under :/cg (ConfigProviderResource)

set(CG_CORE_TEST_SRCS
  app_paths_test.cpp
  changeable_test.cpp
  config_element_complex_test.cpp
  config_element_test.cpp
  config_provider_decorators_test.cpp
  config_provider_folder_test.cpp
  config_provider_ini_test.cpp
  config_provider_zip_test.cpp
  content_detector_test.cpp
  content_type_subst_test.cpp
  content_type_test.cpp
  delimited_text_test.cpp
  delphi_text_test.cpp
  delphi_format_test.cpp
  config_color_test.cpp
  explorer_sort_test.cpp
  file_times_test.cpp
  garbage_collector_thread_test.cpp
  guid_set_test.cpp
  hash_test.cpp
  header_text_test.cpp
  language_manager_test.cpp
  ini_file_test.cpp
  log_test.cpp
  notifier_test.cpp
  operation_notifier_test.cpp
  path_config_test.cpp
  background_task_test.cpp
  read_write_state_test.cpp
  thread_config_test.cpp
  result_test.cpp
  string_config_data_element_test.cpp
  snapshot_test.cpp
  text_codec_test.cpp
  version_test.cpp
  zmp_roundtrip_test.cpp
  content_type_func_test.cpp
  termination_signal_test.cpp
  git_commit_script_test.cpp
  qt_compat_test.cpp
  file_ops_test.cpp
  thread_os_test.cpp
  parse_double_test.cpp
  shared_mutex_test.cpp
  corpus_skip_test.cpp
)
cg_add_test(core_test ${CG_CORE_TEST_SRCS} ${CG_CORE_TEST_RES})
target_link_libraries(core_test PRIVATE cg_core Qt6::Core)
target_compile_definitions(core_test PRIVATE
  CG_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

# version_test compares versionString() with the CMake PROJECT_VERSION.
set_source_files_properties(version_test.cpp PROPERTIES COMPILE_DEFINITIONS "CG_PROJECT_VERSION=\"${PROJECT_VERSION}\"")
