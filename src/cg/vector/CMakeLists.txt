# Vector module. Files are added as the module grows.
add_library(cg_vector STATIC
  category.cpp
  html_to_hint_text_converter_stuped.cpp
  appearance_of_mark_point.cpp
  appearance_of_mark_line.cpp
  appearance_of_mark_polygon.cpp
  appearance_of_mark_factory.cpp
  import_config.cpp
  vector_data_item_base.cpp
  vector_data_factory_simple.cpp
  vector_item_subset.cpp
  vector_item_subset_builder.cpp
  vector_item_tree.cpp
  config_provider_helpers.cpp
  xml_escape.cpp
  xml_streaming_writer.cpp
  vector_data_factory_for_map.cpp
  vector_item_tree_importer_by_vector_loader.cpp
  vector_item_tree_importer_list.cpp
  vector_item_tree_exporter_list.cpp
  appearance_helper.cpp
  export_marks_2_kml_config.cpp
  jpeg_with_exif_import_config.cpp
)
target_include_directories(cg_vector PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cg_vector
  PUBLIC  cg_core cg_geo Qt6::Core Qt6::Gui
  PRIVATE cg_warnings
)
