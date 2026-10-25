# marks module skeleton (docs/specs/marks.md section 9). Files are added as the marks tickets land.
add_library(cg_marks STATIC
  mormot/mark_system_orm_cache_base.cpp # T-0063
  mormot/marks/mark_db_impl_orm_cache.cpp # T-0076
  mark_category.cpp # T-0064
  mark_category_db_by_impl.cpp # T-0081
  mark_category_list.cpp # T-0064
  mark_category_tree.cpp # T-0064
  mark_db_by_impl.cpp # T-0078
  mark_system_error_handler.cpp # T-0078
  mark_system_helpers.cpp # T-0064
  mark_full_base.cpp # T-0068
  mark_factory.cpp # T-0068
  mark_system_config.cpp # T-0069
  mormot/mark_system_impl_config_orm.cpp # T-0069
  mormot/system_nocase.cpp # T-0062
  mormot/mark_system_orm_model.cpp # T-0062
  mormot/mark_system_orm_tools.cpp # T-0062
  mormot/mark_system_orm_transaction.cpp # T-0066
  mormot/mark_system_orm_statement.cpp # T-0095
  mormot/mark_system_impl_orm_client_provider.cpp # T-0066
  mormot/categories/mark_category_db_impl_orm_cache.cpp # T-0067
  mormot/categories/mark_category_db_impl_orm_helper.cpp # T-0086
  mormot/categories/mark_category_db_impl_orm.cpp # T-0099
  mormot/marks/mark_id_orm.cpp # T-0080
  mormot/marks/mark_factory_db_internal_orm.cpp # T-0083
  mormot/marks/mark_db_impl_orm_helper_write.cpp # T-0095
  mormot/marks/mark_db_impl_orm_helper_update.cpp # T-0100
  mormot/marks/mark_db_impl_orm_helper_read.cpp # T-0105
  mormot/marks/mark_db_impl_orm_helper_query.cpp # T-0111
  mormot/marks/mark_db_impl_orm.cpp # T-0114
  mormot/marks/mark_db_impl_orm_query.cpp # T-0125 (O17)
  mormot/categories/mark_category_db_internal_orm.cpp # T-0077
  mormot/categories/mark_category_factory_db_internal_orm.cpp # T-0077
  mark_category_factory.cpp # T-0091
  mark_category_factory_config.cpp # T-0091
  mark_name_generator.cpp # T-0091
  mark_templates.cpp # T-0091
  mark_template_config_base.cpp # T-0098
  mark_point_template_config.cpp # T-0098
  mark_line_template_config.cpp # T-0098
  mark_poly_template_config.cpp # T-0098
  mark_factory_config.cpp # T-0098
  mark_system_impl_changeable.cpp # T-0102
  mark_system_impl_factory_changeable.cpp # T-0133
  mark_system.cpp # T-0137
  mormot/mark_system_impl_orm.cpp # T-0133
  mormot/mark_system_impl_factory_orm.cpp # T-0133
  sml/extended80.cpp # T-0109
  sml/geometry_from_stream_sml.cpp # T-0109
  sml/geometry_to_stream_sml.cpp # T-0109
  sml/mark_id.cpp # T-0112
  sml/mark_category_sml_db_internal.cpp # T-0112
  sml/mark_category_factory_sml_db_internal.cpp # T-0112
  sml/mark_factory_sml_db_internal.cpp # T-0121
  used_marks_config.cpp # T-0147
  used_marks_config_static.cpp # T-0147
  vector_item_tree_marks_db.cpp # T-0152
)
target_include_directories(cg_marks PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cg_marks
  PUBLIC  cg_core cg_geo cg_vector Qt6::Core
  PRIVATE cg_storage cg_warnings  # cg_storage: sqlite/sqlite_db.h only (T-0062)
)
