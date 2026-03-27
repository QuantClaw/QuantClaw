# Copyright 2025 QuantClaw Contributors
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED QC_CXX_COMPILER OR NOT DEFINED QC_SOURCE_DIR OR NOT DEFINED QC_BINARY_DIR)
  message(FATAL_ERROR "QC_CXX_COMPILER, QC_SOURCE_DIR, and QC_BINARY_DIR must be set")
endif()

if(NOT DEFINED QC_PROJECT_VERSION OR NOT DEFINED QC_BUILD_DATE OR NOT DEFINED QC_GIT_COMMIT)
  message(FATAL_ERROR
    "QC_PROJECT_VERSION, QC_BUILD_DATE, and QC_GIT_COMMIT must be set")
endif()

file(REMOVE_RECURSE "${QC_BINARY_DIR}/gcm.cache")

set(_cxx_flags
  -std=c++23
  -fmodules-ts
  -O2
  -Wall
  -Wextra
  -Wpedantic
  -DQUANTCLAW_VERSION=\"${QC_PROJECT_VERSION}\"
  -DQUANTCLAW_BUILD_DATE=\"${QC_BUILD_DATE}\"
  -DQUANTCLAW_GIT_COMMIT=\"${QC_GIT_COMMIT}\"
)

set(_project_includes
  -I${QC_SOURCE_DIR}/include
  -I${QC_SOURCE_DIR}/src
  -I${QC_SOURCE_DIR}/build-vcpkg/vcpkg_installed/x64-linux/include
  -I${QC_SOURCE_DIR}/build-ninja/vcpkg_installed/x64-linux/include
)

execute_process(
  COMMAND "${QC_CXX_COMPILER}" -dumpfullversion
  OUTPUT_VARIABLE _gcc_full_version
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT _gcc_full_version)
  execute_process(
    COMMAND "${QC_CXX_COMPILER}" -dumpversion
    OUTPUT_VARIABLE _gcc_full_version
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
endif()

string(REGEX MATCH "^[0-9]+" _gcc_major_version "${_gcc_full_version}")

set(_stdlib_std_source "")
foreach(_candidate
        "/usr/include/c++/${_gcc_major_version}/bits/std.cc"
        "/usr/include/c++/${_gcc_full_version}/bits/std.cc")
  if(EXISTS "${_candidate}")
    set(_stdlib_std_source "${_candidate}")
    break()
  endif()
endforeach()

set(_stdlib_compat_source "")
foreach(_candidate
        "/usr/include/c++/${_gcc_major_version}/bits/std.compat.cc"
        "/usr/include/c++/${_gcc_full_version}/bits/std.compat.cc")
  if(EXISTS "${_candidate}")
    set(_stdlib_compat_source "${_candidate}")
    break()
  endif()
endforeach()

if(_stdlib_std_source)
  execute_process(
    COMMAND "${QC_CXX_COMPILER}" ${_cxx_flags} ${_project_includes} -x c++ -fmodule-only -c "${_stdlib_std_source}"
    WORKING_DIRECTORY "${QC_BINARY_DIR}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
  )
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR
      "Precompile failed for libstdc++ std module: ${_stdlib_std_source}\n"
      "stdout:\n${_stdout}\n"
      "stderr:\n${_stderr}")
  endif()
endif()

if(_stdlib_compat_source)
  execute_process(
    COMMAND "${QC_CXX_COMPILER}" ${_cxx_flags} ${_project_includes} -x c++ -fmodule-only -c "${_stdlib_compat_source}"
    WORKING_DIRECTORY "${QC_BINARY_DIR}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
  )
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR
      "Precompile failed for libstdc++ std.compat module: ${_stdlib_compat_source}\n"
      "stdout:\n${_stdout}\n"
      "stderr:\n${_stderr}")
  endif()
endif()

function(qc_run_compile label)
  set(options)
  set(oneValueArgs FILE MODE)
  cmake_parse_arguments(QC "${options}" "${oneValueArgs}" "" ${ARGN})

  if(NOT QC_FILE)
    message(FATAL_ERROR "qc_run_compile(${label}) requires FILE")
  endif()

  if(NOT EXISTS "${QC_FILE}")
    message(STATUS "Skipping missing ${label}: ${QC_FILE}")
    return()
  endif()

  if(QC_MODE STREQUAL "module")
    set(_mode_args -x c++ -fmodule-only -c)
  elseif(QC_MODE STREQUAL "header")
    set(_mode_args -x c++-user-header -c)
  elseif(QC_MODE STREQUAL "system-header")
    set(_mode_args -x c++-system-header -c)
  else()
    message(FATAL_ERROR "qc_run_compile(${label}) requires MODE=module|header|system-header")
  endif()

  execute_process(
    COMMAND "${QC_CXX_COMPILER}" ${_cxx_flags} ${_project_includes} ${_mode_args} "${QC_FILE}"
    WORKING_DIRECTORY "${QC_BINARY_DIR}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
  )

  if(NOT _result EQUAL 0)
    message(FATAL_ERROR
      "Precompile failed for ${label}: ${QC_FILE}\n"
      "stdout:\n${_stdout}\n"
      "stderr:\n${_stderr}")
  endif()
endfunction()

# Foundational named modules first.
qc_run_compile("quantclaw.constants" FILE "${QC_SOURCE_DIR}/src/constants.cppm" MODE module)
qc_run_compile("quantclaw.common.defer" FILE "${QC_SOURCE_DIR}/src/common/defer.cppm" MODE module)

# Third-party header units used throughout the project.
qc_run_compile("nlohmann/json.hpp" FILE "${QC_SOURCE_DIR}/include/nlohmann/json.hpp" MODE header)
qc_run_compile("spdlog/spdlog.h" FILE "${QC_SOURCE_DIR}/build-vcpkg/vcpkg_installed/x64-linux/include/spdlog/spdlog.h" MODE system-header)
qc_run_compile("spdlog/sinks/null_sink.h" FILE "${QC_SOURCE_DIR}/build-vcpkg/vcpkg_installed/x64-linux/include/spdlog/sinks/null_sink.h" MODE system-header)
qc_run_compile("httplib.h" FILE "${QC_SOURCE_DIR}/build-vcpkg/vcpkg_installed/x64-linux/include/httplib.h" MODE system-header)
qc_run_compile("sqlite3.h" FILE "${QC_SOURCE_DIR}/build-vcpkg/vcpkg_installed/x64-linux/include/sqlite3.h" MODE system-header)
qc_run_compile("ixwebsocket/IXHttp.h" FILE "${QC_SOURCE_DIR}/build-vcpkg/vcpkg_installed/x64-linux/include/ixwebsocket/IXHttp.h" MODE system-header)
qc_run_compile("ixwebsocket/IXWebSocket.h" FILE "${QC_SOURCE_DIR}/build-vcpkg/vcpkg_installed/x64-linux/include/ixwebsocket/IXWebSocket.h" MODE system-header)
qc_run_compile("ixwebsocket/IXWebSocketServer.h" FILE "${QC_SOURCE_DIR}/build-vcpkg/vcpkg_installed/x64-linux/include/ixwebsocket/IXWebSocketServer.h" MODE system-header)

# The nlohmann.json named module exports the header unit above.
qc_run_compile("nlohmann.json" FILE "${QC_SOURCE_DIR}/src/thirdparty/json.ixx" MODE module)

