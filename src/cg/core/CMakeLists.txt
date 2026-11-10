# Idioms, logging, hashing, text codecs, IniFile, the config-provider tree, path/app config
# and content types; the GC tick is core::Notifier + GarbageCollectorThread.
add_library(cg_core STATIC
  archive_reader_zip.cpp
  app_paths.cpp
  background_task.cpp
  changeable.cpp
  config_element.cpp
  config_element_complex.cpp
  config_provider.cpp
  config_provider_folder.cpp
  config_provider_ini.cpp
  config_provider_ini_section.cpp
  config_provider_resource.cpp
  config_provider_virtual.cpp
  config_provider_with_global.cpp
  config_provider_zip.cpp
  config_writer.cpp
  content_detector.cpp
  content_type.cpp
  content_type_subst.cpp
  delimited_text.cpp
  guid_set.cpp
  guid_list_static.cpp
  explorer_sort.cpp
  garbage_collector_thread.cpp
  hash.cpp
  header_text.cpp
  ini_file.cpp
  ini_value_codec.cpp
  language_list_static.cpp
  language_manager.cpp
  log.cpp
  log_ring_buffer.cpp
  main_config.cpp
  notifier.cpp
  operation_notifier.cpp
  path_config.cpp
  read_write_state_internal.cpp
  read_write_state_internal_by_other.cpp
  thread_config.cpp
  delphi_text.cpp
  delphi_format.cpp
  config_color.cpp
  string_by_language.cpp
  string_config_data_element_with_def_base.cpp
  string_config_data_element_with_def_by_get_text.cpp
  string_config_data_element_with_def_by_string_rec.cpp
  string_config_data_element_with_language.cpp
  text_codec.cpp
  version.cpp
  platform/file_times.cpp
  platform/thread_os.cpp
  platform/exe_dir.cpp
  platform/parse_double.cpp
  platform/log_noise.cpp
  content_type_func.cpp
  platform/termination_signal.cpp
  platform/terminal.cpp
  platform/file_ops.cpp
)
find_package(LibArchive REQUIRED)  # As storage
target_include_directories(cg_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cg_core
  PUBLIC  Qt6::Core cg_warnings
  PRIVATE cityhash LibArchive::LibArchive
)
# Only version.cpp reads CG_GIT_HASH. As a target-wide definition it changed the command line of every cg_core
# unit on each commit, so no compilation cache entry survived a commit or matched another branch.
set_source_files_properties(version.cpp PROPERTIES COMPILE_DEFINITIONS "CG_GIT_HASH=\"${CG_GIT_HASH}\";CG_PROJECT_VERSION=\"${PROJECT_VERSION}\"")
# The one version source is PROJECT_VERSION (CMakeLists.txt); versionString() no longer holds a copy.
