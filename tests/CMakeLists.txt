include(GoogleTest)
function(cg_add_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE GTest::gtest GTest::gtest_main cg_warnings)
  gtest_discover_tests(${name})
endfunction()
add_subdirectory(core)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/net/CMakeLists.txt")
  add_subdirectory(net)  # T-0010A / T-0012A
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/script/CMakeLists.txt")
  add_subdirectory(script)  # T-0013
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/geo/CMakeLists.txt")
  add_subdirectory(geo)  # T-0016
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/storage/CMakeLists.txt")
  add_subdirectory(storage)  # T-0021A
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/maptype/CMakeLists.txt")
  add_subdirectory(maptype)  # T-0044
endif()
# --- vector (T-0047) ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vector/CMakeLists.txt")
  add_subdirectory(vector)  # T-0047
endif()
# --- marks (T-0062, T-0063, T-0064) ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/marks/CMakeLists.txt")
  add_subdirectory(marks)  # T-0062, T-0063, T-0064
endif()
# --- bitmap (T-0155) ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bitmap/CMakeLists.txt")
  add_subdirectory(bitmap)  # T-0155
endif()
# --- region (T-0157) ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/region/CMakeLists.txt")
  add_subdirectory(region)  # T-0157
endif()
# --- app (T-0184) ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app/CMakeLists.txt")
  add_subdirectory(app)  # T-0184
endif()
