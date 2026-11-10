# Tests of the vector module. Test files are added here as the module grows.
set(CG_VECTOR_TEST_SRCS
  html_to_hint_text_converter_stuped_test.cpp
  category_test.cpp
  appearance_test.cpp
  appearance_of_mark_factory_test.cpp
  import_config_test.cpp
  vector_data_item_test.cpp
  vector_item_subset_test.cpp
  vector_item_tree_test.cpp
  sort_func_test.cpp
  config_provider_helpers_test.cpp
  xml_escape_test.cpp
  xml_streaming_writer_test.cpp
  vector_data_factory_for_map_test.cpp
  vector_item_tree_importer_by_vector_loader_test.cpp
  vector_item_tree_importer_list_test.cpp
  vector_item_tree_exporter_list_test.cpp
  appearance_helper_test.cpp
  export_marks_2_kml_config_test.cpp
  jpeg_with_exif_import_config_test.cpp
)
cg_add_test(vector_test ${CG_VECTOR_TEST_SRCS})
target_link_libraries(vector_test PRIVATE cg_vector cg_core Qt6::Core)
target_include_directories(vector_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(vector_test PRIVATE CG_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
