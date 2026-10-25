# T-0047: vector module skeleton (docs/specs/vector.md §9). Files are added as the V-tickets land.
add_library(cg_vector STATIC
  category.cpp                          # T-0047
  html_to_hint_text_converter_stuped.cpp # T-0047
  appearance_of_mark_point.cpp          # T-0047
  appearance_of_mark_line.cpp           # T-0047
  appearance_of_mark_polygon.cpp        # T-0047
  appearance_of_mark_factory.cpp        # T-0050
  import_config.cpp                     # T-0052
  vector_data_item_base.cpp # T-0051
  vector_data_factory_simple.cpp # T-0051
  vector_item_subset.cpp # T-0057
  vector_item_subset_builder.cpp # T-0057
  vector_item_tree.cpp # T-0057
  config_provider_helpers.cpp           # T-0058
  xml_escape.cpp # T-0130
  xml_streaming_writer.cpp # T-0130
  vector_data_factory_for_map.cpp       # T-0129
  vector_item_tree_importer_by_vector_loader.cpp # T-0129
  vector_item_tree_importer_list.cpp # T-0141
  vector_item_tree_exporter_list.cpp # T-0141
  appearance_helper.cpp                 # T-0142
  export_marks_2_kml_config.cpp # T-0151
  jpeg_with_exif_import_config.cpp # T-0151
)
target_include_directories(cg_vector PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cg_vector
  PUBLIC  cg_core cg_geo Qt6::Core Qt6::Gui
  PRIVATE cg_warnings
)
