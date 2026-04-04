cmake_minimum_required(VERSION 3.20)
project(quantclaw VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# Sanitizer options (mutually exclusive: ASAN and TSAN cannot be combined)
option(ENABLE_ASAN "Build with AddressSanitizer + LeakSanitizer" OFF)
option(ENABLE_TSAN "Build with ThreadSanitizer" OFF)
option(ENABLE_UBSAN "Build with UndefinedBehaviorSanitizer" OFF)

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

# Required dependencies
find_package(spdlog REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

# nlohmann_json: use FetchContent to ensure consistent version across all environments
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# IXWebSocket: try vcpkg first, fallback to FetchContent
set(USE_TLS ON CACHE BOOL "" FORCE)
set(USE_OPEN_SSL ON CACHE BOOL "" FORCE)
set(USE_MBED_TLS OFF CACHE BOOL "" FORCE)
find_package(ixwebsocket CONFIG QUIET)
if(ixwebsocket_FOUND)
    # vcpkg provides ixwebsocket::ixwebsocket
    set(IXWEBSOCKET_TARGET ixwebsocket::ixwebsocket)
else()
    FetchContent_Declare(
        ixwebsocket
        GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
        GIT_TAG v11.4.6
        GIT_SHALLOW TRUE
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
        GIT_TAG v0.18.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(httplib)
endif()

# Source files - explicitly listed
set(QUANTCLAW_CORE_SOURCES
    # Core
    src/core/agent_loop.cpp
    src/core/config.cpp
    src/core/memory_manager.cpp
    src/core/skill_loader.cpp
    src/core/prompt_builder.cpp
    src/core/signal_handler.cpp
    src/core/session_compaction.cpp
    src/core/cron_scheduler.cpp
    src/core/memory_search.cpp
    src/core/context_pruner.cpp
    src/core/usage_accumulator.cpp
    src/core/message_commands.cpp
    src/core/subagent.cpp
    src/core/default_context_engine.cpp
    src/core/multi_stage_compaction.cpp
    src/core/vector_index.cpp
    src/core/mmr_reranker.cpp
    src/core/temporal_decay.cpp
    # Providers / auth
    src/auth/provider_auth.cpp
    src/auth/openai_codex_auth.cpp
    src/auth/github_copilot_auth.cpp
    src/providers/curl_raii.cpp
    src/providers/openai_provider.cpp
    src/providers/openai_codex_provider.cpp
    src/providers/github_copilot_provider.cpp
    src/providers/anthropic_provider.cpp
    src/providers/provider_registry.cpp
    src/providers/provider_error.cpp
    src/providers/cooldown_tracker.cpp
    src/providers/failover_resolver.cpp
    # CLI
    src/cli/cli_manager.cpp
    src/cli/commands.cpp
    src/cli/model_auth_commands.cpp
    src/cli/gateway_commands.cpp
    src/cli/agent_commands.cpp
    src/cli/session_commands.cpp
    src/cli/onboard_commands.cpp
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
# Git commit hash (short, 7 chars). Falls back to "unknown" outside a repo.
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

target_include_directories(quantclaw_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
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

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(quantclaw_core PRIVATE -Wall -Wextra -O2)
    target_compile_options(quantclaw PRIVATE -Wall -Wextra -O2)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(quantclaw_core PRIVATE /W4 /Od /RTC1 /EHsc)
        target_compile_options(quantclaw PRIVATE /W4 /Od /RTC1 /EHsc)
    else()
        target_compile_options(quantclaw_core PRIVATE /W4 /O2 /EHsc)
        target_compile_options(quantclaw PRIVATE /W4 /O2 /EHsc)
    endif()
endif()

install(TARGETS quantclaw DESTINATION bin)

# --- Tests ---
if(BUILD_TESTS)
    # googletest: try vcpkg/system first, fallback to FetchContent
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.15.2
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(googletest)
    endif()

    enable_testing()

    set(TEST_SOURCES
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
        tests/test_openai_provider.cpp
        tests/test_openai_codex_auth.cpp
        tests/test_openai_codex_provider.cpp
        tests/test_github_copilot_auth.cpp
        tests/test_github_copilot_provider.cpp
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
        tests/test_model_auth_commands.cpp
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
    )

    add_executable(quantclaw_tests ${TEST_SOURCES})
    target_link_libraries(quantclaw_tests PRIVATE
        quantclaw_core
        GTest::gtest_main
    )
    target_include_directories(quantclaw_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_compile_options(quantclaw_tests PRIVATE /W4 /wd4100 /Od /RTC1 /EHsc)
        else()
            target_compile_options(quantclaw_tests PRIVATE /W4 /wd4100 /O2 /EHsc)
        endif()
    endif()

    include(GoogleTest)
    if(WIN32)
        # On Windows, vcpkg DLLs are in vcpkg_installed/<triplet>/bin; add them
        # to PATH so gtest_discover_tests can run the binary to list tests, and
        # so ctest can run the tests themselves.
        set(VCPKG_DLL_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin")
        # ENVIRONMENT entries are a CMake semicolon-separated list, so the
        # PATH value must have its semicolons escaped as \; to avoid being
        # split into separate list items.
        string(REPLACE ";" "\\;" _escaped_system_path "$ENV{PATH}")
        gtest_discover_tests(quantclaw_tests
            DISCOVERY_MODE PRE_TEST
            PROPERTIES ENVIRONMENT "PATH=${VCPKG_DLL_DIR}\;${_escaped_system_path}"
        )
    else()
        gtest_discover_tests(quantclaw_tests)
    endif()
endif()
