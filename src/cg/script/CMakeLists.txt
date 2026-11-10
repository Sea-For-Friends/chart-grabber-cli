# Lexer, parser, AST, diagnostics and CompiledScript; value.cpp + interpreter.cpp (the interpreter core);
# delphi_format_adapter.cpp (Format/FormatEx over script::Value; the formatter is in core); regex_compat.cpp
# (RegExprGetMatchSubStr/RegExprReplaceMatchSubStr + the TRegExpr->QRegularExpression pattern rewrite);
# url_script.cpp (UrlScript, the BuildRequest contract). script_host.cpp (a production IScriptHost) is
# maptype's, not this module's.
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
