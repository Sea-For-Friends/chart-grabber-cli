# Script mode (T-0200, T-0264, docs/specs/app-cli.md 2.6): writes ${OUT} with `#define CG_GIT_COMMIT "<12 hex>[-dirty]"`
# ("unknown" when no source gives a commit). The file is rewritten only when its text changes, so main.cpp is not
# recompiled when the commit did not change. Input: -DSRC=<source dir> -DOUT=<header> [-DOVERRIDE=<12 hex>[-dirty]].
# Precedence: (a) OVERRIDE (cache variable CHART_GRABBER_COMMIT), verbatim; (b) git, only when SRC is itself the work-tree
# top; (c) the archive stamp cli/archive_commit.txt (`git archive` substitutes the hash, .gitattributes export-subst);
# (d) unknown.
# CMake regular expressions have no {n} repetition: the patterns are built with string(REPEAT).
string(REPEAT "[0-9a-f]" 12 hex12)
string(REPEAT "[0-9a-f]" 40 hex40)
set(commit "unknown")
set(found FALSE)
if(NOT "${OVERRIDE}" STREQUAL "")
  if(NOT "${OVERRIDE}" MATCHES "^${hex12}(-dirty)?$")
    message(FATAL_ERROR "CHART_GRABBER_COMMIT must be 12 lowercase hex digits, optionally followed by -dirty; got '${OVERRIDE}'")
  endif()
  set(commit "${OVERRIDE}")
  set(found TRUE)
endif()
if(NOT found)
  find_program(GIT_EXECUTABLE git)
  if(GIT_EXECUTABLE)
    # An archive extracted inside another repository must not print that repository's HEAD: SRC must be the top itself.
    execute_process(COMMAND ${GIT_EXECUTABLE} -C ${SRC} rev-parse --show-toplevel
                    OUTPUT_VARIABLE top ERROR_QUIET RESULT_VARIABLE top_rc OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(top_rc EQUAL 0 AND NOT "${top}" STREQUAL "")
      if(WIN32)
        # T-0347: MSYS2 git prints the top as /c/dir where the native cmake has C:/dir: bring it to the cmake form.
        find_program(CYGPATH_EXECUTABLE cygpath)
        if(CYGPATH_EXECUTABLE)
          execute_process(COMMAND ${CYGPATH_EXECUTABLE} -m "${top}"
                          OUTPUT_VARIABLE top_m ERROR_QUIET RESULT_VARIABLE top_m_rc OUTPUT_STRIP_TRAILING_WHITESPACE)
          if(top_m_rc EQUAL 0 AND NOT "${top_m}" STREQUAL "")
            set(top "${top_m}")
          endif()
        endif()
        if("${top}" MATCHES "^/([A-Za-z])(/.*)?$")
          set(top "${CMAKE_MATCH_1}:${CMAKE_MATCH_2}")
        endif()
      endif()
      get_filename_component(top_real "${top}" REALPATH)
      get_filename_component(src_real "${SRC}" REALPATH)
      if("${top_real}" STREQUAL "${src_real}")
        execute_process(COMMAND ${GIT_EXECUTABLE} -C ${SRC} rev-parse HEAD
                        OUTPUT_VARIABLE head ERROR_QUIET RESULT_VARIABLE head_rc OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(head_rc EQUAL 0 AND head MATCHES "^[0-9a-f]+$")
          string(SUBSTRING "${head}" 0 12 commit)
          set(found TRUE)
          # -dirty counts build inputs only: a new top-level build input must be added to this list.
          execute_process(COMMAND ${GIT_EXECUTABLE} -C ${SRC} status --porcelain --untracked-files=no --
                            CMakeLists.txt CMakePresets.json LICENSE cmake cli src third_party data
                          OUTPUT_VARIABLE changes ERROR_QUIET RESULT_VARIABLE status_rc)
          if(NOT status_rc EQUAL 0 OR NOT "${changes}" STREQUAL "")
            set(commit "${commit}-dirty")
          endif()
        endif()
      endif()
    endif()
  endif()
endif()
if(NOT found AND EXISTS "${SRC}/cli/archive_commit.txt")
  file(STRINGS "${SRC}/cli/archive_commit.txt" stamp LIMIT_COUNT 1)
  if("${stamp}" MATCHES "^${hex40}([0-9a-f]+)?$")
    string(SUBSTRING "${stamp}" 0 12 commit)
  endif()
endif()
set(text "#define CG_GIT_COMMIT \"${commit}\"\n")
set(old "")
if(EXISTS "${OUT}")
  file(READ "${OUT}" old)
endif()
if(NOT "${old}" STREQUAL "${text}")
  file(WRITE "${OUT}" "${text}")
endif()
