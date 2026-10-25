# T-0013 slice: lexer + parser + AST + the corpus parse gate.
# T-0014 slice: value_test.cpp + interpreter_test.cpp.
# T-0014C1 slice: delphi_format_test.cpp. T-0014C2a slice: regex_compat_test.cpp.
# T-0015A slice: script_host_fake.cpp (the shared IScriptHost test double, no TEST cases of its
# own - see script.md §11.1) feeds interpreter_test.cpp's host-binding cases.
# T-0015C slice: url_script_test.cpp (spec docs/specs/script.md §11.8; two cases needing interface
# calls - NestedHttpRequestUsesTheFakeHost, GlobalStoreSharedBetweenTwoUrlScriptsOfTheSameMap -
# move to T-0015E).
# T-0015E slice: corpus_run_test.cpp - runs (not just parses) all 350 GetUrlScript.txt fixtures
# through UrlScript, structural checks only; the two moved url_script_test.cpp cases above land
# here instead.
# T-0015D slice: golden_url_test.cpp (spec docs/specs/script.md §11.5 - 21 hand-derived golden
# URLs, tests/fixtures/script-goldens/).
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
