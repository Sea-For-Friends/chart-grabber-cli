# T-0010A slice: request/result model, HTTP status taxonomy, download checker interface, the one
# IDownloader interface, and the file:// URL table. T-0012A slice: raw-header-blob helpers,
# gzip/deflate/br/zstd content decoding, MIME sniffing. Qt Core only -- no libcurl, no Qt Network
# in the downloader itself (see tools/validate.sh's QNAM-IN-DOWNLOADER rule).
# T-0011A slice: proxy_config.cpp, inet_config.cpp, tile_downloader_config.cpp, proxy_resolver.cpp,
# download_stats.cpp -- the settings half of net. proxy_resolver.cpp is the one file allowed to
# touch Qt6::Network (system proxy detection only, tech-lead-rulings.md 2026-09-17 net.md §10.4).
# T-0011B slice: retry_downloader.cpp, download_queue.cpp -- the queue/worker-pool topology and
# the retry/pacing decorator (docs/specs/net.md §2.15). T-0012B slice: anti_ban.cpp,
# simple_http_downloader.cpp (docs/specs/net.md §9.1 has the full, eventual source list).
# T-0010B slice: curl_global/curl_share/curl_session/curl_downloader/downloader_ttl -- the blocking
# libcurl transport and its 5-minute TTL wrapper. The GC tick comes from cg_core (Notifier, T-0048).
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED) # T-0011A: CURLAUTH_* constants; T-0010B: full libcurl calls (curl_session.cpp)
find_package(PkgConfig REQUIRED)
pkg_check_modules(BROTLIDEC REQUIRED IMPORTED_TARGET libbrotlidec)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)

add_library(cg_net STATIC
  download_result.cpp
  download_result_factory.cpp
  http_status.cpp
  file_url.cpp
  http_headers.cpp
  content_decoder.cpp
  proxy_config.cpp
  inet_config.cpp
  global_download_config.cpp  # T-0118
  tile_downloader_config.cpp
  proxy_resolver.cpp
  download_stats.cpp
  curl_global.cpp
  curl_share.cpp
  curl_session.cpp
  curl_downloader.cpp
  downloader_ttl.cpp
  anti_ban.cpp
  simple_http_downloader.cpp
  retry_downloader.cpp
  download_queue.cpp
)
target_include_directories(cg_net PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cg_net
  PUBLIC  Qt6::Core cg_warnings cg_core
  PRIVATE ZLIB::ZLIB PkgConfig::BROTLIDEC PkgConfig::ZSTD CURL::libcurl Qt6::Network
)
