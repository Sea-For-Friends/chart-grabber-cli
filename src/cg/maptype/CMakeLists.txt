# The module and its small static configuration types. The storage, script and net links are
# added with the files that need them.
find_package(ZLIB REQUIRED) # crc32 of the sample index
add_library(cg_maptype STATIC
  log_maptype.cpp
  zmp_config.cpp
  map_abilities_config.cpp
  map_coverage_info.cpp
  layer_draw_config.cpp
  tile_post_download_crop_config.cpp
  math_expression_evaluator.cpp
  tile_download_request_builder_config.cpp
  url_template.cpp
  map_version_config.cpp
  map_version_factory_changeable.cpp
  map_version_changeable.cpp
  map_version_request_config.cpp
  map_version_request_changeable.cpp
  map_type_gui_config.cpp
  map_type_set.cpp
  map_type_set_changeable_simple.cpp
  zmp_info.cpp
  pascal_script_global.cpp
  pascal_script_logger.cpp
  pascal_script_tile_cache.cpp
  tile_downloader_state.cpp
  tile_request.cpp
  zmp_file_names_iterator_factory.cpp
  zmp_info_set.cpp
  map_type_list.cpp
  map_type_hot_key_list_static.cpp
  map_type_gui_config_list.cpp
  tile_download_request_builder.cpp
  tile_download_request_builder_lazy.cpp
  tile_download_request_builder_pascal_script.cpp
  tile_download_request_builder_factory_pascal_script.cpp
  coord_converter_simple_by_projection_set.cpp
  tile_request_result.cpp
  tile_request_task.cpp
  predicate_by_static_sample_list.cpp
  download_checker_stuped.cpp
  tile_download_result_saver_stuped.cpp
  tile_download_subsystem_state.cpp
  tile_download_subsystem.cpp
  map_type_set_changeable_by_source_set_with_filter.cpp
  active_map_config.cpp
  active_layers_config.cpp
  map_type.cpp
  map_types_main_list.cpp
  content_converter_bitmap.cpp
  content_converter_matrix.cpp
  content_converters.cpp
  source_json_container.cpp
  js_shared_store.cpp
  js_script_watchdog.cpp
  js_bridge.cpp
  tile_download_request_builder_js.cpp
  tile_download_request_builder_factory_js.cpp
)
target_include_directories(cg_maptype PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cg_maptype
  PUBLIC  cg_core cg_geo cg_storage cg_script cg_net cg_vector cg_bitmap Qt6::Core Qt6::Gui Qt6::Network
  PRIVATE cg_warnings ZLIB::ZLIB Qt6::Qml
)
set_target_properties(cg_maptype PROPERTIES AUTOMOC ON) # JsBridge is the first moc class
