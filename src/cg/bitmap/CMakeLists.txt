# T-0155: bitmap module, image model and factories (docs/specs/bitmap.md §2.1, §2.2, §9).
# Codec libraries (JPEG, PNG, WebP) and Qt6::Gui are added by the codec tickets T-0160..T-0162.
add_library(cg_bitmap STATIC
  bitmap32_static.cpp                # T-0155
  bitmap32_buffer_factory.cpp        # T-0155
  bitmap32_buffer_factory_simple.cpp # T-0155
  bitmap32_static_factory.cpp        # T-0155
  bitmap_tile_lib_jpeg.cpp           # T-0160
  bitmap_tile_free_image.cpp         # T-0161
  bitmap_tile_free_image_webp.cpp    # T-0162
  bitmap_tile_save_load_factory.cpp  # T-0163
  content_type_manager_bitmap.cpp    # T-0163
)
target_include_directories(cg_bitmap PUBLIC ${CMAKE_SOURCE_DIR}/src)
find_package(JPEG REQUIRED) # T-0160
find_package(PNG REQUIRED) # T-0161
find_package(PkgConfig REQUIRED) # T-0162
pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp libwebpmux) # T-0162
target_link_libraries(cg_bitmap PUBLIC cg_core Qt6::Core Qt6::Gui PRIVATE JPEG::JPEG PNG::PNG PkgConfig::WEBP)
