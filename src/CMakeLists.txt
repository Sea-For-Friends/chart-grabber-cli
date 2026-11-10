# Module list grows as modules are ported. Keep dependency order.
add_subdirectory(cg/core)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cg/net/CMakeLists.txt")
  add_subdirectory(cg/net)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cg/script/CMakeLists.txt")
  add_subdirectory(cg/script)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cg/geo/CMakeLists.txt")
  add_subdirectory(cg/geo)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cg/storage/CMakeLists.txt")
  add_subdirectory(cg/storage)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cg/maptype/CMakeLists.txt")
  add_subdirectory(cg/maptype)
endif()
# --- vector ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cg/vector/CMakeLists.txt")
  add_subdirectory(cg/vector)
endif()
# --- marks ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cg/marks/CMakeLists.txt")
  add_subdirectory(cg/marks)
endif()
# --- bitmap ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cg/bitmap/CMakeLists.txt")
  add_subdirectory(cg/bitmap)
endif()

# --- region ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cg/region/CMakeLists.txt")
  add_subdirectory(cg/region)
endif()
# --- app ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cg/app/CMakeLists.txt")
  add_subdirectory(cg/app)
endif()
