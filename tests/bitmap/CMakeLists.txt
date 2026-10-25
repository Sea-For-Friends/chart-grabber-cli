# tests of the bitmap module (docs/specs/bitmap.md section 9). Later bitmap tickets append here.
set(CG_BITMAP_TEST_SRCS
  bitmap32_factories_test.cpp # T-0155
  bitmap_tile_lib_jpeg_test.cpp # T-0160
  bitmap_tile_free_image_test.cpp # T-0161
  bitmap_tile_free_image_webp_test.cpp # T-0162
  bitmap_tile_save_load_factory_test.cpp # T-0163
)
find_package(JPEG REQUIRED) # the test decodes with libjpeg directly as its reference
find_package(PkgConfig REQUIRED)
pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp) # the test decodes WebP with libwebp as its reference
cg_add_test(bitmap_test ${CG_BITMAP_TEST_SRCS})
target_link_libraries(bitmap_test PRIVATE cg_bitmap cg_core Qt6::Core JPEG::JPEG PkgConfig::WEBP)
target_compile_definitions(bitmap_test PRIVATE CG_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
