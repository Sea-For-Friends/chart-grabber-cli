# http_status/download_result/download_result_factory/file_url tests.
# download_request.h/download_checker.h/downloader.h are header-only interfaces with no free
# functions to unit test directly; the checker's NullDownloadChecker and the interfaces are
# exercised by their consumers. Raw-header-blob helpers, content decoding, MIME sniffing and
# proxy_config/inet_config/tile_downloader_config have their own tests. proxy_resolver.cpp:
# proxy_resolver_test.cpp (the environment bypass); the rest of it is exercised by its consumers.
# download_stats.cpp: download_stats_test.cpp (start values of DownloadInfo).
# curl_downloader_test.cpp (the HttpTestServer suite) plus the 4 file_url I/O cases.
# http_test_server.cpp is a fixture (QTcpServer-based), not itself a test binary.
# retry_downloader_test.cpp and download_queue_test.cpp -- both use a
# FakeDownloader/FakeDownloaderFactory double, no real network and no real 60 s wait (an injected
# cg::core::Notifier ticked by hand with an injected clock). anti_ban_test.cpp,
# simple_http_downloader_test.cpp.
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
  global_download_config_test.cpp
  curl_downloader_test.cpp
  anti_ban_test.cpp
  simple_http_downloader_test.cpp
  retry_downloader_test.cpp
  download_queue_test.cpp
  downloader_ttl_test.cpp
  curl_session_thread_test.cpp
  download_stats_test.cpp
  proxy_resolver_test.cpp
  user_agent_fallback_test.cpp
)
cg_add_test(net_test ${CG_NET_TEST_SRCS} http_test_server.cpp)
# The Brotli *encoder* is only needed to synthesize compressed fixtures in-test
# (content_decoder_test.cpp); production code (cg_net) only links the decoder.
find_package(PkgConfig REQUIRED)
find_package(CURL REQUIRED) # user_agent_fallback_test.cpp calls curl_version_info
pkg_check_modules(BROTLIENC REQUIRED IMPORTED_TARGET libbrotlienc)
target_link_libraries(net_test PRIVATE cg_net cg_core Qt6::Core Qt6::Network PkgConfig::BROTLIENC CURL::libcurl)
target_compile_definitions(net_test PRIVATE
  CG_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
# No test may reach the real network. gtest_discover_tests (inside cg_add_test) registers one
# CTest test per TEST case, not one named "net_test", so there is no single CTest target name
# to attach ENVIRONMENT to here; curl_downloader_test.cpp sets CG_NET_NO_EXTERNAL_NETWORK
# itself (a static initializer, before any test body runs) instead.
# RefusesExternalHostUnderTestGuard is the regression test that this is actually wired up.
