# Module list grows as tickets land. Keep dependency order.
add_subdirectory(cg/core)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cg/net/CMakeLists.txt")
  add_subdirectory(cg/net)  # T-0010A / T-0012A
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cg/script/CMakeLists.txt")
  add_subdirectory(cg/script)  # T-0013
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cg/geo/CMakeLists.txt")
  add_subdirectory(cg/geo)  # T-0016
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cg/storage/CMakeLists.txt")
  add_subdirectory(cg/storage)  # T-0021A
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cg/maptype/CMakeLists.txt")
  add_subdirectory(cg/maptype)  # T-0044
endif()
# --- vector (T-0047) ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cg/vector/CMakeLists.txt")
  add_subdirectory(cg/vector)  # T-0047
endif()
# --- marks (T-0062, T-0063, T-0064) ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cg/marks/CMakeLists.txt")
  add_subdirectory(cg/marks)  # T-0062, T-0063, T-0064
endif()
# --- bitmap (T-0155) ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cg/bitmap/CMakeLists.txt")
  add_subdirectory(cg/bitmap)  # T-0155
endif()

# --- region (T-0157) ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cg/region/CMakeLists.txt")
  add_subdirectory(cg/region)  # T-0157
endif()
# --- app (T-0184) ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cg/app/CMakeLists.txt")
  add_subdirectory(cg/app)  # T-0184
endif()
