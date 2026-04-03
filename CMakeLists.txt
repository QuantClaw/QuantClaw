cmake_minimum_required(VERSION 3.31)
project(quantclaw VERSION 0.3.0 LANGUAGES CXX)

# Machine-targeted build policy: this branch is GCC-only.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR
        "QuantClaw local C++23/modules build is configured for GCC only. "
        "Reconfigure with GCC (e.g. CC=gcc CXX=g++ cmake ...).")
endif()

if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)
    message(FATAL_ERROR
        "GCC 15+ is required for this C++23/modules configuration. "
        "Detected GCC ${CMAKE_CXX_COMPILER_VERSION}.")
endif()

if(NOT (CMAKE_GENERATOR STREQUAL "Ninja" OR
        CMAKE_GENERATOR STREQUAL "Ninja Multi-Config"))
    message(FATAL_ERROR
        "C++ modules require Ninja generator in this configuration. "
        "Configure with the CMake preset or: cmake -G Ninja ...")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

# Match direct `c++ -fmodules-ts` builds for GCC modules.
add_compile_options(-fmodules-ts)

option(QC_PRECOMPILE_GCC_STDLIB_MODULES
    "Precompile GCC libstdc++ std/std.compat module units for import std;"
    ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Embed RPATH so the binary finds system libraries even when
# LD_LIBRARY_PATH points elsewhere (e.g. miniconda).
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)
if(UNIX AND NOT APPLE)
    # Add architecture-specific system lib path (Debian/Ubuntu multiarch)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,/usr/lib/x86_64-linux-gnu")
    elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath,/usr/lib/aarch64-linux-gnu")
    endif()
endif()

option(BUILD_TESTS "Build tests" ON)

# Prefer the workspace's existing vcpkg install trees when present.
foreach(_qc_vcpkg_root
        "${CMAKE_SOURCE_DIR}/build-vcpkg/vcpkg_installed/x64-linux"
        "${CMAKE_SOURCE_DIR}/build-ninja/vcpkg_installed/x64-linux"
        "${CMAKE_SOURCE_DIR}/build-coverage/vcpkg_installed/x64-linux")
    if(EXISTS "${_qc_vcpkg_root}")
        list(PREPEND CMAKE_PREFIX_PATH "${_qc_vcpkg_root}")
    endif()
endforeach()

# Sanitizer options (mutually exclusive: ASAN and TSAN cannot be combined)
option(ENABLE_ASAN  "Build with AddressSanitizer + LeakSanitizer"   OFF)
option(ENABLE_TSAN  "Build with ThreadSanitizer"                    OFF)
option(ENABLE_UBSAN "Build with UndefinedBehaviorSanitizer"         OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "ENABLE_ASAN and ENABLE_TSAN are mutually exclusive.")
endif()

# Sanitizers use -fsanitize= flags which are GCC/Clang only.
if(NOT MSVC)
    if(ENABLE_ASAN)
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
        message(STATUS "AddressSanitizer enabled")
    endif()

    if(ENABLE_TSAN)
        add_compile_options(-fsanitize=thread)
        add_link_options(-fsanitize=thread)
        message(STATUS "ThreadSanitizer enabled")
    endif()

    if(ENABLE_UBSAN)
        add_compile_options(-fsanitize=undefined -fno-sanitize-recover=all)
        add_link_options(-fsanitize=undefined)
        message(STATUS "UndefinedBehaviorSanitizer enabled")
    endif()
elseif(ENABLE_ASAN OR ENABLE_TSAN OR ENABLE_UBSAN)
    message(WARNING "Sanitizers are not supported with MSVC; flags ignored.")
endif()

# Coverage (gcov) — works with C++23 modules since instrumentation is per-TU.
option(ENABLE_COVERAGE "Build with gcov coverage instrumentation" OFF)
if(ENABLE_COVERAGE)
    add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
    add_link_options(--coverage)
    message(STATUS "Coverage instrumentation enabled (gcov)")
endif()

# Required dependencies
include(FetchContent)
find_package(GTest CONFIG REQUIRED)
find_package(spdlog CONFIG QUIET)
find_package(CURL CONFIG QUIET)
if(NOT CURL_FOUND)
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG        curl-8_11_1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(curl)
endif()
find_package(OpenSSL REQUIRED)
find_package(DuckDB CONFIG QUIET)
if(NOT DuckDB_FOUND)
    find_package(duckdb CONFIG QUIET)
endif()
if(TARGET DuckDB::DuckDB)
    set(QC_DUCKDB_TARGET DuckDB::DuckDB)
elseif(TARGET duckdb::duckdb)
    set(QC_DUCKDB_TARGET duckdb::duckdb)
elseif(TARGET duckdb)
    set(QC_DUCKDB_TARGET duckdb)
elseif(TARGET duckdb_static)
    set(QC_DUCKDB_TARGET duckdb_static)
else()
    message(FATAL_ERROR "DuckDB CMake target not found after package discovery")
endif()
find_package(nlohmann_json CONFIG QUIET)

# nlohmann_json: use FetchContent to ensure consistent version across all environments
if(NOT nlohmann_json_FOUND)
    if(NOT DEFINED fetchcontent_included)
        include(FetchContent)
        set(fetchcontent_included TRUE)
    endif()
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

if(NOT spdlog_FOUND)
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG        v1.14.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(spdlog)
endif()

# IXWebSocket: try vcpkg first, fallback to FetchContent
set(USE_TLS ON CACHE BOOL "" FORCE)
set(USE_OPEN_SSL ON CACHE BOOL "" FORCE)
set(USE_MBED_TLS OFF CACHE BOOL "" FORCE)
find_package(ixwebsocket CONFIG QUIET)
if(ixwebsocket_FOUND)
    # vcpkg provides ixwebsocket::ixwebsocket
    set(IXWEBSOCKET_TARGET ixwebsocket::ixwebsocket)
else()
    include(FetchContent)
    FetchContent_Declare(
        ixwebsocket
        GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
        GIT_TAG        v11.4.6
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(ixwebsocket)
    # FetchContent provides just ixwebsocket
    set(IXWEBSOCKET_TARGET ixwebsocket)
endif()

# cpp-httplib: try vcpkg first, fallback to FetchContent
set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)
find_package(httplib CONFIG QUIET)
if(NOT httplib_FOUND)
    if(NOT DEFINED fetchcontent_included)
        include(FetchContent)
        set(fetchcontent_included TRUE)
    endif()
    set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "" FORCE)
    # Only require zlib if available; avoids link failure on minimal systems
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        set(HTTPLIB_REQUIRE_ZLIB ON CACHE BOOL "" FORCE)
    else()
        set(HTTPLIB_REQUIRE_ZLIB OFF CACHE BOOL "" FORCE)
    endif()
    FetchContent_Declare(
        httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG        v0.18.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(httplib)
endif()

# Source files - .cpp implementation units only
# Module interface units (.cppm/.ixx) are in QUANTCLAW_CORE_MODULE_SOURCES below.
set(QUANTCLAW_CORE_SOURCES
    # Providers
    src/providers/provider_registry.cpp
    # Core
    src/core/config.cpp
    src/core/agent_loop.cpp
    src/core/cron_scheduler.cpp
    src/core/dag_runtime.cpp
    src/core/mmr_reranker.cpp
    src/core/default_context_engine.cpp
    src/core/message_commands.cpp
    src/core/context_pruner.cpp
    src/core/memory_manager.cpp
    src/core/memory_search.cpp
    src/core/multi_stage_compaction.cpp
    src/core/prompt_builder.cpp
    src/core/session_compaction.cpp
    src/core/signal_handler.cpp
    src/core/skill_loader.cpp
    src/core/temporal_decay.cpp
    src/core/usage_accumulator.cpp
    src/core/vector_index.cpp
    src/core/subagent.cpp
    # CLI
    src/cli/cli_manager.cpp
    src/cli/agent_commands.cpp
    src/cli/gateway_commands.cpp
    src/cli/onboard_commands.cpp
    src/cli/session_commands.cpp
    # Gateway
    src/gateway/gateway_server.cpp
    src/gateway/gateway_client.cpp
    src/gateway/rpc_handlers.cpp
    src/gateway/daemon_manager.cpp
    src/gateway/command_queue.cpp
    # Session
    src/session/session_manager.cpp
    src/session/session_maintenance.cpp
    # Tools
    src/tools/tool_registry.cpp
    src/tools/tool_chain.cpp
    src/tools/browser_tool.cpp
    # MCP
    src/mcp/mcp_server.cpp
    src/mcp/mcp_client.cpp
    src/mcp/mcp_tool_manager.cpp
    # Channels
    src/channels/adapter_manager.cpp
    src/channels/channel_policy.cpp
    # Plugins
    src/plugins/plugin_manifest.cpp
    src/plugins/plugin_registry.cpp
    src/plugins/sidecar_manager.cpp
    src/plugins/hook_manager.cpp
    src/plugins/plugin_system.cpp
    # Security
    src/security/sandbox.cpp
    src/security/tool_permissions.cpp
    src/security/exec_approval.cpp
    src/security/rbac.cpp
    src/security/rate_limiter.cpp
    # Web
    src/web/web_server.cpp
    src/web/api_routes.cpp
    # IPC - single TCP implementation for all platforms
    src/platform/ipc_tcp.cpp
)

set(QUANTCLAW_CORE_MODULE_SOURCES
    src/constants.cppm
    src/config.cppm
    src/common/defer.cppm
    src/providers/llm_provider.cppm
    src/providers/embedding_provider.cppm
    src/providers/curl_raii.cppm
    src/providers/anthropic_provider.cppm
    src/providers/provider_registry.cppm
    src/providers/provider_error.cppm
    src/providers/cooldown_tracker.cppm
    src/providers/failover_resolver.cppm
    src/core/content_block.cppm
    src/core/agent_loop.cppm
    src/core/cron_scheduler.cppm
    src/core/dag_runtime.cppm
    src/core/mmr_reranker.cppm
    src/core/context_engine.cppm
    src/core/default_context_engine.cppm
    src/core/message_commands.cppm
    src/core/context_pruner.cppm
    src/core/memory_manager.cppm
    src/core/memory_search.cppm
    src/core/multi_stage_compaction.cppm
    src/core/prompt_builder.cppm
    src/core/session_compaction.cppm
    src/core/signal_handler.cppm
    src/core/skill_loader.cppm
    src/core/temporal_decay.cppm
    src/core/usage_accumulator.cppm
    src/core/vector_index.cppm
    src/core/subagent.cppm
    src/session/session_manager.cppm
    src/security/rbac.cppm
    src/platform/process.cppm
    src/platform/ipc.cppm
    src/platform/service.cppm
    src/cli/cli_manager.cppm
    src/cli/agent_commands.cppm
    src/cli/gateway_commands.cppm
    src/cli/onboard_commands.cppm
    src/cli/session_commands.cppm
    src/gateway/daemon_manager.cppm
    src/gateway/rpc_handlers.cppm
    src/gateway/protocol.cppm
    src/gateway/gateway_server.cppm
    src/gateway/gateway_client.cppm
    src/gateway/command_queue.cppm
    src/channels/channel_policy.cppm
    src/plugins/plugin_manifest.cppm
    src/plugins/plugin_registry.cppm
    src/plugins/plugin_system.cppm
    src/plugins/hook_manager.cppm
    src/plugins/sidecar_manager.cppm
    src/common/noncopyable.cppm
    src/common/parse_util.cppm
    src/common/string_util.cppm
    src/builtin_skills.cppm
    src/thirdparty/json.ixx
    src/tools/tool_registry.cppm
    src/session/session_maintenance.cppm
    src/tools/tool_chain.cppm
    src/mcp/mcp_server.cppm
    src/mcp/mcp_client.cppm
    src/mcp/mcp_tool_manager.cppm
    src/mcp/quantclaw_mcp_tools.cppm
    src/channels/adapter_manager.cppm
    src/security/rate_limiter.cppm
    src/security/tool_permissions.cppm
    src/security/exec_approval.cppm
    src/tools/browser_tool.cppm
    src/security/sandbox.cppm
    src/web/web_server.cppm
    src/web/api_routes.cppm
)

# Platform-specific sources (process + service abstractions)
if(WIN32)
    list(APPEND QUANTCLAW_CORE_SOURCES
        src/platform/process_win32.cpp
        src/platform/service_win32.cpp
    )
else()
    list(APPEND QUANTCLAW_CORE_SOURCES
        src/platform/process_unix.cpp
        src/platform/service_unix.cpp
    )
endif()

# --- Build-time metadata ---
# Git commit hash (short, 7 chars).  Falls back to "unknown" outside a repo.
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE QC_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT QC_GIT_COMMIT)
    set(QC_GIT_COMMIT "unknown")
endif()
string(TIMESTAMP QC_BUILD_DATE "%Y-%m-%d")

add_library(quantclaw_core STATIC ${QUANTCLAW_CORE_SOURCES})
set_property(TARGET quantclaw_core PROPERTY CXX_SCAN_FOR_MODULES ON)

target_sources(quantclaw_core
    PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
        FILES ${QUANTCLAW_CORE_MODULE_SOURCES}
)

execute_process(
    COMMAND ${CMAKE_COMMAND}
            -DQC_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DQC_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DQC_BINARY_DIR=${CMAKE_BINARY_DIR}
            -DQC_PROJECT_VERSION=${PROJECT_VERSION}
            -DQC_BUILD_DATE=${QC_BUILD_DATE}
            -DQC_GIT_COMMIT=${QC_GIT_COMMIT}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/precompile_gcm_cache.cmake
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    RESULT_VARIABLE QC_PRECOMPILE_GCM_RESULT
)
if(NOT QC_PRECOMPILE_GCM_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Failed to precompile header units/modules into gcm.cache before build")
endif()

target_include_directories(quantclaw_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(quantclaw_core PUBLIC
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    QUANTCLAW_VERSION="${PROJECT_VERSION}"
    QUANTCLAW_BUILD_DATE="${QC_BUILD_DATE}"
    QUANTCLAW_GIT_COMMIT="${QC_GIT_COMMIT}"
    CPPHTTPLIB_OPENSSL_SUPPORT
)

target_link_libraries(quantclaw_core
    PUBLIC
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        ${IXWEBSOCKET_TARGET}
        httplib::httplib
    PRIVATE
        CURL::libcurl
        OpenSSL::SSL
        OpenSSL::Crypto
        ${QC_DUCKDB_TARGET}
)

# WinSock2 and BCrypt (needed by mbedtls) required on Windows
if(WIN32)
    target_link_libraries(quantclaw_core PUBLIC ws2_32 bcrypt)
endif()

add_executable(quantclaw src/main.cpp)
target_link_libraries(quantclaw PRIVATE quantclaw_core)
target_include_directories(quantclaw PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
set_property(TARGET quantclaw PROPERTY CXX_SCAN_FOR_MODULES ON)

if(QC_PRECOMPILE_GCC_STDLIB_MODULES)
    # GCC/libstdc++ ships C++23 named module sources at bits/std.cc and
    # bits/std.compat.cc. Build them once into this build tree so sources can
    # use `import std;` and `import std.compat;` reliably.
    execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} -dumpfullversion
        OUTPUT_VARIABLE QC_GCC_FULL_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT QC_GCC_FULL_VERSION)
        execute_process(
            COMMAND ${CMAKE_CXX_COMPILER} -dumpversion
            OUTPUT_VARIABLE QC_GCC_FULL_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()

    string(REGEX MATCH "^[0-9]+" QC_GCC_MAJOR "${QC_GCC_FULL_VERSION}")

    set(QC_GCC_STD_MODULE_SOURCE "")
    foreach(_std_src
            "/usr/include/c++/${QC_GCC_MAJOR}/bits/std.cc"
            "/usr/include/c++/${QC_GCC_FULL_VERSION}/bits/std.cc")
        if(EXISTS "${_std_src}")
            set(QC_GCC_STD_MODULE_SOURCE "${_std_src}")
            break()
        endif()
    endforeach()

    set(QC_GCC_STD_COMPAT_MODULE_SOURCE "")
    foreach(_std_compat_src
            "/usr/include/c++/${QC_GCC_MAJOR}/bits/std.compat.cc"
            "/usr/include/c++/${QC_GCC_FULL_VERSION}/bits/std.compat.cc")
        if(EXISTS "${_std_compat_src}")
            set(QC_GCC_STD_COMPAT_MODULE_SOURCE "${_std_compat_src}")
            break()
        endif()
    endforeach()

    if(QC_GCC_STD_MODULE_SOURCE)
        set(QC_GCC_STD_MODULE_DIR
            "${CMAKE_BINARY_DIR}/generated/stdlib_modules")
        file(MAKE_DIRECTORY "${QC_GCC_STD_MODULE_DIR}")
        configure_file("${QC_GCC_STD_MODULE_SOURCE}"
            "${QC_GCC_STD_MODULE_DIR}/std.cc"
            COPYONLY)
        target_sources(quantclaw_core
            PUBLIC
                FILE_SET CXX_MODULES
                BASE_DIRS ${QC_GCC_STD_MODULE_DIR}
                FILES
                    ${QC_GCC_STD_MODULE_DIR}/std.cc
        )
        if(QC_GCC_STD_COMPAT_MODULE_SOURCE)
            configure_file("${QC_GCC_STD_COMPAT_MODULE_SOURCE}"
                "${QC_GCC_STD_MODULE_DIR}/std.compat.cc"
                COPYONLY)
            target_sources(quantclaw_core
                PUBLIC
                    FILE_SET CXX_MODULES
                    BASE_DIRS ${QC_GCC_STD_MODULE_DIR}
                    FILES
                        ${QC_GCC_STD_MODULE_DIR}/std.compat.cc
            )
        endif()

        message(STATUS "Enabled libstdc++ named module precompile for import std;")
    else()
        message(WARNING
            "QC_PRECOMPILE_GCC_STDLIB_MODULES=ON but libstdc++ std.cc was not found. "
            "`import std;` may fail on this host.")
    endif()
endif()

# Compiler flags (GCC-only)
target_compile_options(quantclaw_core PRIVATE -Wall -Wextra -Wpedantic -O2)
target_compile_options(quantclaw PRIVATE -Wall -Wextra -Wpedantic -O2)

install(TARGETS quantclaw DESTINATION bin)

# --- Tests ---
if(BUILD_TESTS)
    enable_testing()

    set(TEST_SOURCES
        tests/test_main.cpp
        tests/test_helpers.cppm
        tests/test_config.cpp
        tests/test_protocol.cpp
        tests/test_session_manager.cpp
        tests/test_gateway.cpp
        tests/test_skill_loader.cpp
        tests/test_memory_manager.cpp
        tests/test_agent_loop.cpp
        tests/test_common_utils.cpp
        tests/test_string_util.cpp
        tests/test_parse_util.cpp
        tests/test_p1_features.cpp
        tests/test_mcp_server.cpp
        tests/test_mcp_integration.cpp
        tests/test_anthropic_provider.cpp
        tests/test_security_sandbox.cpp
        tests/test_tool_registry.cpp
        tests/test_tool_chain.cpp
        tests/test_tool_permissions.cpp
        tests/test_mcp_tool_manager.cpp
        tests/test_web_server.cpp
        tests/test_api_routes.cpp
        tests/test_prompt_builder.cpp
        tests/test_mcp_client.cpp
        tests/test_daemon_manager.cpp
        tests/test_cli_commands.cpp
        tests/test_rpc_handlers.cpp
        tests/test_e2e.cpp
        tests/test_plugin_system.cpp
        tests/test_channel_policy.cpp
        tests/test_advanced_features.cpp
        tests/test_exec_approval.cpp
        tests/test_provider_registry.cpp
        tests/test_session_maintenance.cpp
        tests/test_subagent.cpp
        tests/test_browser_tool.cpp
        tests/test_platform.cpp
        tests/test_rbac.cpp
        tests/test_rate_limiter.cpp
        tests/test_command_queue.cpp
        tests/test_failover.cpp
        tests/test_context_management.cpp
        tests/test_session_compaction.cpp
        tests/test_message_commands.cpp
        tests/test_builtin_skills.cpp
        tests/test_agent_commands.cpp
        tests/test_context_engine.cpp
        tests/test_multi_stage_compaction.cpp
        tests/test_hybrid_memory_search.cpp
        tests/test_dag_runtime.cpp
    )

    add_executable(quantclaw_tests ${TEST_SOURCES})
    target_sources(quantclaw_tests
        PUBLIC
            FILE_SET CXX_MODULES
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/tests
            FILES
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_helpers.cppm
    )
    target_link_libraries(quantclaw_tests PRIVATE
        quantclaw_core
        GTest::gtest
        GTest::gtest_main
    )
    set_property(TARGET quantclaw_tests PROPERTY CXX_SCAN_FOR_MODULES ON)
    target_compile_options(quantclaw_tests PRIVATE -Wall -Wextra -Wpedantic -O2)
    target_include_directories(quantclaw_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(NAME quantclaw_tests COMMAND quantclaw_tests)

    if(TARGET qc_stdlib_modules)
        add_dependencies(quantclaw_tests qc_stdlib_modules)
    endif()
endif()



