# T-0010A slice: http_status/download_result/download_result_factory/file_url tests.
# download_request.h/download_checker.h/downloader.h are header-only interfaces with no free
# functions to unit test directly; the checker's NullDownloadChecker and the interfaces are
# exercised by their consumers (T-0010B onward).
# T-0012A slice: raw-header-blob helpers, content decoding, MIME sniffing.
# T-0011A slice: proxy_config/inet_config/tile_downloader_config (29 cases, §7.7-§7.9).
# proxy_resolver.cpp: proxy_resolver_test.cpp (T-0201, the environment bypass); the rest of it is
# exercised by consumers landing in T-0010B/T-0011B/T-0012B.
# download_stats.cpp: download_stats_test.cpp (T-0156, start values of DownloadInfo).
# T-0010B slice: curl_downloader_test.cpp (the HttpTestServer suite, §7.10) plus the 4 file_url
# I/O cases moved here from T-0010A (per ruling, docs/handoffs/T-0010A.md open questions).
# http_test_server.cpp is a fixture (QTcpServer-based), not itself a test binary.
# T-0011B slice: retry_downloader_test.cpp (§7.11) and download_queue_test.cpp (§7.12) -- both use
# a FakeDownloader/FakeDownloaderFactory double, no real network and no real 60 s wait (an injected
# cg::core::Notifier ticked by hand with an injected clock; T-0048).
# T-0012B slice: anti_ban_test.cpp, simple_http_downloader_test.cpp (§7.13, §7.14).
set(CG_NET_TEST_SRCS
  http_status_test.cpp
  download_result_test.cpp
  download_result_factory_test.cpp
  file_url_test.cpp
  http_headers_test.cpp
  content_decoder_test.cpp
  proxy_config_test.cpp
  inet_config_test.cpp
  tile_downloader_config_test.cpp
  global_download_config_test.cpp  # T-0118
  curl_downloader_test.cpp
  anti_ban_test.cpp
  simple_http_downloader_test.cpp
  retry_downloader_test.cpp
  download_queue_test.cpp
  downloader_ttl_test.cpp
  curl_session_thread_test.cpp  # T-0144
  download_stats_test.cpp  # T-0156
  proxy_resolver_test.cpp  # T-0201
)
cg_add_test(net_test ${CG_NET_TEST_SRCS} http_test_server.cpp)
# The Brotli *encoder* is only needed to synthesize compressed fixtures in-test
# (content_decoder_test.cpp); production code (cg_net) only links the decoder.
find_package(PkgConfig REQUIRED)
pkg_check_modules(BROTLIENC REQUIRED IMPORTED_TARGET libbrotlienc)
target_link_libraries(net_test PRIVATE cg_net cg_core Qt6::Core Qt6::Network PkgConfig::BROTLIENC)
target_compile_definitions(net_test PRIVATE
  CG_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
# docs/specs/net.md §7.0: no test may reach the real network. gtest_discover_tests (inside
# cg_add_test) registers one CTest test per TEST case, not one named "net_test", so there is no
# single CTest target name to attach ENVIRONMENT to here; curl_downloader_test.cpp sets
# CG_NET_NO_EXTERNAL_NETWORK itself (a static initializer, before any test body runs) instead.
# RefusesExternalHostUnderTestGuard is the regression test that this is actually wired up.
