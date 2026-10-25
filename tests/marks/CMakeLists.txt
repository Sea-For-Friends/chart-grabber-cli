# tests of the marks module (docs/specs/marks.md section 7). Later marks tickets append here.
set(CG_MARKS_TEST_SRCS
  mark_system_orm_cache_base_test.cpp # T-0063
  mark_db_impl_orm_cache_test.cpp # T-0076
  mark_category_test.cpp # T-0064
  mark_category_list_to_tree_test.cpp # T-0064
  mark_interfaces_test.cpp # T-0064
  mark_full_base_test.cpp # T-0068
  mark_factory_test.cpp # T-0068
  mark_system_config_test.cpp # T-0069
  mark_db_by_impl_test.cpp # T-0078
  catch_exception_test.cpp # T-0078
  mark_id_orm_test.cpp # T-0080
  mark_category_db_by_impl_test.cpp # T-0081
  system_nocase_test.cpp # T-0062
  mark_system_orm_model_test.cpp # T-0062
  mark_system_impl_orm_client_provider_test.cpp # T-0066
  mark_category_db_impl_orm_cache_test.cpp # T-0067
  mark_category_db_impl_orm_helper_test.cpp # T-0086
  mark_category_db_internal_orm_test.cpp # T-0077
  mark_category_factory_db_internal_orm_test.cpp # T-0077
  mark_category_factory_config_test.cpp # T-0091
  mark_category_factory_test.cpp # T-0091
  mark_name_generator_test.cpp # T-0091
  mark_templates_test.cpp # T-0091
  mark_template_configs_test.cpp # T-0098
  mark_factory_db_internal_orm_test.cpp # T-0083
  mark_db_impl_orm_helper_write_test.cpp # T-0095
  mark_db_impl_orm_helper_update_test.cpp # T-0100
  mark_system_impl_changeable_test.cpp # T-0102
  mark_category_db_impl_orm_test.cpp # T-0099
  mark_db_impl_orm_helper_read_test.cpp # T-0105
  extended80_test.cpp # T-0109
  geometry_sml_test.cpp # T-0109
  mark_system_helpers_import_test.cpp # T-0106
  mark_db_impl_orm_helper_query_test.cpp # T-0111
  mark_id_sml_test.cpp # T-0112
  mark_category_sml_db_internal_test.cpp # T-0112
  mark_category_factory_sml_db_internal_test.cpp # T-0112
  mark_db_impl_orm_test.cpp # T-0114
  mark_factory_sml_db_internal_test.cpp # T-0121
  mark_system_impl_orm_test.cpp # T-0133
  mark_system_test.cpp # T-0137
  used_marks_config_test.cpp # T-0147
  vector_item_tree_marks_db_test.cpp # T-0152
)
cg_add_test(marks_test ${CG_MARKS_TEST_SRCS})
target_link_libraries(marks_test PRIVATE cg_marks cg_storage cg_vector cg_core Qt6::Core)
target_include_directories(marks_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(marks_test PRIVATE
  CG_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
