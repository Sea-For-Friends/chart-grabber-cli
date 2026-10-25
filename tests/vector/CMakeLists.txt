# T-0047: tests of the V1/V2 units (docs/specs/vector.md §7.2). Later vector tickets append here.
set(CG_VECTOR_TEST_SRCS
  html_to_hint_text_converter_stuped_test.cpp # T-0047
  category_test.cpp                           # T-0047
  appearance_test.cpp                         # T-0047
  appearance_of_mark_factory_test.cpp         # T-0050
  import_config_test.cpp                      # T-0052
  vector_data_item_test.cpp                   # T-0051
  vector_item_subset_test.cpp # T-0057
  vector_item_tree_test.cpp # T-0057
  sort_func_test.cpp # T-0057
  config_provider_helpers_test.cpp            # T-0058
  xml_escape_test.cpp # T-0130
  xml_streaming_writer_test.cpp # T-0130
  vector_data_factory_for_map_test.cpp        # T-0129
  vector_item_tree_importer_by_vector_loader_test.cpp # T-0129
  vector_item_tree_importer_list_test.cpp # T-0141
  vector_item_tree_exporter_list_test.cpp # T-0141
  appearance_helper_test.cpp                  # T-0142
  export_marks_2_kml_config_test.cpp # T-0151
  jpeg_with_exif_import_config_test.cpp # T-0151
)
cg_add_test(vector_test ${CG_VECTOR_TEST_SRCS})
target_link_libraries(vector_test PRIVATE cg_vector cg_core Qt6::Core)
target_include_directories(vector_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(vector_test PRIVATE CG_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures") # T-0058
