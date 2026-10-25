# T-0002A+B: idioms, logging, hashing, text codecs, IniFile (slice A) plus the config-provider
# tree, path/app config, content types (slice B, docs/specs/core.md section 10.2 +
# the GC tick is core::Notifier + GarbageCollectorThread).
add_library(cg_core STATIC
  archive_reader_zip.cpp  # T-0061
  app_paths.cpp
  background_task.cpp  # T-0056
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
  config_provider_zip.cpp  # T-0061
  config_writer.cpp
  content_detector.cpp
  content_type.cpp
  content_type_subst.cpp
  delimited_text.cpp
  guid_set.cpp  # T-0055
  guid_list_static.cpp  # T-0055
  explorer_sort.cpp
  garbage_collector_thread.cpp  # T-0046
  hash.cpp
  header_text.cpp
  ini_file.cpp
  ini_value_codec.cpp
  language_list_static.cpp  # T-0043
  language_manager.cpp
  log.cpp
  log_ring_buffer.cpp
  main_config.cpp
  notifier.cpp
  operation_notifier.cpp
  path_config.cpp
  read_write_state_internal.cpp  # T-0056
  read_write_state_internal_by_other.cpp  # T-0056
  thread_config.cpp  # T-0056
  delphi_text.cpp  # T-0045
  delphi_format.cpp  # T-0092 (moved from script)
  config_color.cpp  # T-0049
  string_by_language.cpp
  string_config_data_element_with_def_base.cpp
  string_config_data_element_with_def_by_get_text.cpp
  string_config_data_element_with_def_by_string_rec.cpp
  string_config_data_element_with_language.cpp
  text_codec.cpp
  version.cpp
  platform/file_times.cpp  # T-0021F1
  platform/thread_os.cpp  # T-0056
  content_type_func.cpp  # T-0183
  platform/termination_signal.cpp  # T-0186
  platform/terminal.cpp  # T-0186
  platform/file_ops.cpp  # T-0335
)
find_package(LibArchive REQUIRED)  # T-0061: ADR-0012, as storage (T-0024)
target_include_directories(cg_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cg_core
  PUBLIC  Qt6::Core cg_warnings
  PRIVATE cityhash LibArchive::LibArchive
)
# T-0090: only version.cpp reads CG_GIT_HASH. As a target-wide definition it changed the command line of every
# cg_core unit on each commit, so no compilation cache entry survived a commit or matched another branch.
set_source_files_properties(version.cpp PROPERTIES COMPILE_DEFINITIONS "CG_GIT_HASH=\"${CG_GIT_HASH}\";CG_PROJECT_VERSION=\"${PROJECT_VERSION}\"")
# T-0328: the one version source is PROJECT_VERSION (CMakeLists.txt); versionString() no longer holds a copy.
