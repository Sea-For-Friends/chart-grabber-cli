# tests of the marks module. Test files are added here as the module grows.
set(CG_MARKS_TEST_SRCS
  mark_system_orm_cache_base_test.cpp
  mark_db_impl_orm_cache_test.cpp
  mark_category_test.cpp
  mark_category_list_to_tree_test.cpp
  mark_interfaces_test.cpp
  mark_full_base_test.cpp
  mark_factory_test.cpp
  mark_system_config_test.cpp
  mark_db_by_impl_test.cpp
  catch_exception_test.cpp
  mark_id_orm_test.cpp
  mark_category_db_by_impl_test.cpp
  system_nocase_test.cpp
  mark_system_orm_model_test.cpp
  mark_system_impl_orm_client_provider_test.cpp
  mark_category_db_impl_orm_cache_test.cpp
  mark_category_db_impl_orm_helper_test.cpp
  mark_category_db_internal_orm_test.cpp
  mark_category_factory_db_internal_orm_test.cpp
  mark_category_factory_config_test.cpp
  mark_category_factory_test.cpp
  mark_name_generator_test.cpp
  mark_templates_test.cpp
  mark_template_configs_test.cpp
  mark_factory_db_internal_orm_test.cpp
  mark_db_impl_orm_helper_write_test.cpp
  mark_db_impl_orm_helper_update_test.cpp
  mark_system_impl_changeable_test.cpp
  mark_category_db_impl_orm_test.cpp
  mark_db_impl_orm_helper_read_test.cpp
  extended80_test.cpp
  geometry_sml_test.cpp
  mark_system_helpers_import_test.cpp
  mark_db_impl_orm_helper_query_test.cpp
  mark_id_sml_test.cpp
  mark_category_sml_db_internal_test.cpp
  mark_category_factory_sml_db_internal_test.cpp
  mark_db_impl_orm_test.cpp
  mark_factory_sml_db_internal_test.cpp
  mark_system_impl_orm_test.cpp
  mark_system_test.cpp
  used_marks_config_test.cpp
  vector_item_tree_marks_db_test.cpp
)
cg_add_test(marks_test ${CG_MARKS_TEST_SRCS})
target_link_libraries(marks_test PRIVATE cg_marks cg_storage cg_vector cg_core Qt6::Core)
target_include_directories(marks_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(marks_test PRIVATE
  CG_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
