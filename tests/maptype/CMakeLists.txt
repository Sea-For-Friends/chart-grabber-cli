# T-0044: tests of the five small static configuration types (docs/specs/maptype.md §2.6.6,
# §2.6.9, §2.7). Later maptype tickets append their test files here.
set(CG_MAPTYPE_TEST_SRCS
  zmp_config_test.cpp                     # T-0044
  map_abilities_config_test.cpp           # T-0044
  map_coverage_info_test.cpp              # T-0044
  layer_draw_config_test.cpp              # T-0044
  tile_post_download_crop_config_test.cpp # T-0044 (reads the stock ZMP corpus)
  math_expression_evaluator_test.cpp      # T-0053
  tile_download_request_builder_config_test.cpp # T-0053
  url_template_test.cpp                   # T-0053 (DUnit port + corpus)
  map_version_test.cpp                    # T-0054
  map_type_gui_config_test.cpp            # T-0054
  map_type_set_test.cpp                   # T-0065
  map_type_set_changeable_simple_test.cpp # T-0065
  zmp_info_test.cpp                       # T-0060
  pascal_script_global_test.cpp           # T-0070
  pascal_script_logger_test.cpp           # T-0070
  pascal_script_tile_cache_test.cpp       # T-0070
  tile_downloader_state_test.cpp          # T-0071
  zmp_info_set_test.cpp                   # T-0072
  map_type_list_test.cpp                  # T-0073
  map_type_hot_key_list_static_test.cpp   # T-0073
  map_type_gui_config_list_test.cpp       # T-0087
  coord_converter_simple_by_projection_set_test.cpp # T-0096
  tile_download_request_builder_test.cpp  # T-0096
  tile_request_result_test.cpp            # T-0108
  tile_request_task_test.cpp              # T-0108
  predicate_by_static_sample_list_test.cpp # T-0113
  download_checker_stuped_test.cpp        # T-0113
  tile_download_result_saver_stuped_test.cpp # T-0115
  tile_download_subsystem_state_test.cpp  # T-0127
  tile_download_subsystem_test.cpp        # T-0127
  http_test_server_shared.cpp             # T-0127: the net tests' fixture server, compiled into this binary too
  map_type_set_changeable_by_source_set_with_filter_test.cpp # T-0146
  active_map_config_test.cpp              # T-0145
  active_layers_config_test.cpp           # T-0145
  map_type_test.cpp                       # T-0140
  map_types_main_list_test.cpp            # T-0150
  content_converters_test.cpp             # T-0120
  source_json_container_test.cpp          # T-0224
  tile_download_request_builder_js_test.cpp # T-0225
  js_script_watchdog_test.cpp             # T-0225
  url_differential_test.cpp               # T-0280
)
cg_add_test(maptype_test ${CG_MAPTYPE_TEST_SRCS})
target_link_libraries(maptype_test PRIVATE cg_maptype cg_vector cg_storage cg_geo cg_core Qt6::Core Qt6::Gui Qt6::Qml)
target_include_directories(maptype_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(maptype_test PRIVATE
  CG_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
