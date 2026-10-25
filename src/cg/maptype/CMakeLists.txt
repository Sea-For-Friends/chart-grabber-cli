# T-0044: module skeleton and the five small static configuration types of docs/specs/maptype.md
# §2.6.6, §2.6.9, §2.7. The storage/script/net links of spec §9.1 are added by the tickets whose
# files need them; nothing here includes them yet.
find_package(ZLIB REQUIRED) # T-0113: crc32 of the sample index
add_library(cg_maptype STATIC
  log_maptype.cpp
  zmp_config.cpp                     # T-0044
  map_abilities_config.cpp           # T-0044
  map_coverage_info.cpp              # T-0044
  layer_draw_config.cpp              # T-0044
  tile_post_download_crop_config.cpp # T-0044
  math_expression_evaluator.cpp      # T-0053
  tile_download_request_builder_config.cpp # T-0053
  url_template.cpp                   # T-0053
  map_version_config.cpp             # T-0054
  map_version_factory_changeable.cpp # T-0054
  map_version_changeable.cpp         # T-0054
  map_version_request_config.cpp     # T-0054
  map_version_request_changeable.cpp # T-0054
  map_type_gui_config.cpp            # T-0054
  map_type_set.cpp                   # T-0065
  map_type_set_changeable_simple.cpp # T-0065
  zmp_info.cpp                       # T-0060
  pascal_script_global.cpp           # T-0070
  pascal_script_logger.cpp           # T-0070
  pascal_script_tile_cache.cpp       # T-0070
  tile_downloader_state.cpp          # T-0071
  tile_request.cpp                   # T-0071
  zmp_file_names_iterator_factory.cpp # T-0072
  zmp_info_set.cpp                   # T-0072
  map_type_list.cpp                  # T-0073
  map_type_hot_key_list_static.cpp   # T-0073
  map_type_gui_config_list.cpp       # T-0087
  tile_download_request_builder.cpp  # T-0096
  tile_download_request_builder_lazy.cpp # T-0096
  tile_download_request_builder_pascal_script.cpp # T-0096
  tile_download_request_builder_factory_pascal_script.cpp # T-0096
  coord_converter_simple_by_projection_set.cpp # T-0096
  tile_request_result.cpp            # T-0108
  tile_request_task.cpp              # T-0108
  predicate_by_static_sample_list.cpp # T-0113
  download_checker_stuped.cpp        # T-0113
  tile_download_result_saver_stuped.cpp # T-0115
  tile_download_subsystem_state.cpp  # T-0127
  tile_download_subsystem.cpp        # T-0127
  map_type_set_changeable_by_source_set_with_filter.cpp # T-0146
  active_map_config.cpp              # T-0145
  active_layers_config.cpp           # T-0145
  map_type.cpp                       # T-0140
  map_types_main_list.cpp            # T-0150
  content_converter_bitmap.cpp       # T-0120
  content_converter_matrix.cpp       # T-0120
  content_converters.cpp             # T-0120
  source_json_container.cpp          # T-0224
  js_shared_store.cpp                # T-0225
  js_script_watchdog.cpp             # T-0225
  js_bridge.cpp                      # T-0225
  tile_download_request_builder_js.cpp # T-0225
  tile_download_request_builder_factory_js.cpp # T-0225
)
target_include_directories(cg_maptype PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cg_maptype
  PUBLIC  cg_core cg_geo cg_storage cg_script cg_net cg_vector cg_bitmap Qt6::Core Qt6::Gui Qt6::Network
  PRIVATE cg_warnings ZLIB::ZLIB Qt6::Qml
)
set_target_properties(cg_maptype PROPERTIES AUTOMOC ON) # T-0225: JsBridge is the first moc class
