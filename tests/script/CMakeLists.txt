# Lexer, parser, AST and the corpus parse gate; value_test.cpp + interpreter_test.cpp;
# delphi_format_test.cpp; regex_compat_test.cpp; script_host_fake.cpp (the shared IScriptHost test
# double, no TEST cases of its own) feeds interpreter_test.cpp's host-binding cases;
# url_script_test.cpp; corpus_run_test.cpp runs (not just parses) all 350 GetUrlScript.txt
# fixtures through UrlScript, structural checks only, and holds the two UrlScript cases that need
# interface calls; golden_url_test.cpp checks 21 hand-derived golden URLs
# (tests/fixtures/script-goldens/).
set(CG_SCRIPT_TEST_SRCS
  lexer_test.cpp
  parser_test.cpp
  corpus_parse_test.cpp
  value_test.cpp
  script_host_fake.cpp
  interpreter_test.cpp
  regex_compat_test.cpp
  url_script_test.cpp
  corpus_run_test.cpp
  golden_url_test.cpp
)
cg_add_test(script_test ${CG_SCRIPT_TEST_SRCS})
target_link_libraries(script_test PRIVATE cg_script cg_core Qt6::Core)
target_compile_definitions(script_test PRIVATE
  CG_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)
