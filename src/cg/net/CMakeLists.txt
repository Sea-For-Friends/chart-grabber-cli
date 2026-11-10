# Request/result model, HTTP status taxonomy, download checker interface, the one IDownloader
# interface and the file:// URL table; raw-header-blob helpers, gzip/deflate/br/zstd content
# decoding and MIME sniffing. Qt Core only -- no libcurl, no Qt Network in the downloader itself
# (the QNAM-IN-DOWNLOADER rule of tools/validate.sh). The settings half: proxy_config.cpp,
# inet_config.cpp, tile_downloader_config.cpp, proxy_resolver.cpp, download_stats.cpp;
# proxy_resolver.cpp is the one file allowed to touch Qt6::Network (system proxy detection only).
# retry_downloader.cpp and download_queue.cpp: the queue/worker-pool topology and the retry/pacing
# decorator. anti_ban.cpp and simple_http_downloader.cpp.
# curl_global/curl_share/curl_session/curl_downloader/downloader_ttl: the blocking libcurl transport
# and its 5-minute TTL wrapper. The GC tick comes from cg_core (Notifier).
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED) # CURLAUTH_* constants; full libcurl calls (curl_session.cpp)
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
  global_download_config.cpp
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
  user_agent_fallback.cpp
)
target_include_directories(cg_net PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cg_net
  PUBLIC  Qt6::Core cg_warnings cg_core
  PRIVATE ZLIB::ZLIB PkgConfig::BROTLIDEC PkgConfig::ZSTD CURL::libcurl Qt6::Network
)
