# Bitmap module, image model and factories. Codec libraries (JPEG, PNG, WebP) and Qt6::Gui are added
# with the codecs.
add_library(cg_bitmap STATIC
  bitmap32_static.cpp
  bitmap32_buffer_factory.cpp
  bitmap32_buffer_factory_simple.cpp
  bitmap32_static_factory.cpp
  bitmap_tile_lib_jpeg.cpp
  bitmap_tile_free_image.cpp
  bitmap_tile_free_image_webp.cpp
  bitmap_tile_save_load_factory.cpp
  content_type_manager_bitmap.cpp
)
target_include_directories(cg_bitmap PUBLIC ${CMAKE_SOURCE_DIR}/src)
find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp libwebpmux)
target_link_libraries(cg_bitmap PUBLIC cg_core Qt6::Core Qt6::Gui PRIVATE JPEG::JPEG PNG::PNG PkgConfig::WEBP)
