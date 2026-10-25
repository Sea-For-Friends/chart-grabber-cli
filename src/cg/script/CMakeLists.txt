# T-0013 slice: lexer, parser, AST, diagnostics, and the "compile" half of CompiledScript.
# T-0014 slice: value.cpp + interpreter.cpp (pure interpreter core, no host binding yet).
# T-0014C1 slice: delphi_format_adapter.cpp (Format/FormatEx over script::Value; formatter in core, T-0092).
# T-0014C2a slice: regex_compat.cpp (RegExprGetMatchSubStr/RegExprReplaceMatchSubStr + the R1-R16
# TRegExpr->QRegularExpression rewrite, spec docs/specs/script.md 2.9/§6).
# T-0015C slice: url_script.cpp (UrlScript, the BuildRequest contract, spec docs/specs/script.md
# 2.13). script_host.cpp (a production IScriptHost) is maptype's, not this module's.
add_library(cg_script STATIC
  diagnostic.cpp
  lexer.cpp
  parser.cpp
  compiled_script.cpp
  value.cpp
  interpreter.cpp
  delphi_format_adapter.cpp
  regex_compat.cpp
  url_script.cpp
)
target_include_directories(cg_script PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cg_script
  PUBLIC Qt6::Core cg_warnings cg_core
)
