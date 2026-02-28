cmake_minimum_required(VERSION 3.20)
project(quantclaw LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(BUILD_TESTS "Build tests" ON)

# Required dependencies
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

# IXWebSocket via FetchContent
include(FetchContent)
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG        v11.4.5
)
set(USE_TLS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ixwebsocket)

FetchContent_Declare(
    cpp-httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.18.3
)
set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cpp-httplib)

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
    src/core/subagent.cpp
    # Providers
    src/providers/openai_provider.cpp
    src/providers/anthropic_provider.cpp
    src/providers/provider_registry.cpp
    # CLI
    src/cli/cli_manager.cpp
    src/cli/commands.cpp
    src/cli/gateway_commands.cpp
    src/cli/agent_commands.cpp
    src/cli/session_commands.cpp
    # Gateway
    src/gateway/gateway_server.cpp
    src/gateway/gateway_client.cpp
    src/gateway/rpc_handlers.cpp
    src/gateway/daemon_manager.cpp
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
    # Web
    src/web/web_server.cpp
    src/web/api_routes.cpp
)

add_library(quantclaw_core STATIC ${QUANTCLAW_CORE_SOURCES})

target_include_directories(quantclaw_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(quantclaw_core
    PUBLIC
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        ixwebsocket
        httplib::httplib
    PRIVATE
        CURL::libcurl
        OpenSSL::SSL
        OpenSSL::Crypto
)

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
    target_compile_options(quantclaw_core PRIVATE /W4 /O2)
    target_compile_options(quantclaw PRIVATE /W4 /O2)
endif()

install(TARGETS quantclaw DESTINATION bin)

# --- Tests ---
if(BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    set(TEST_SOURCES
        tests/test_config.cpp
        tests/test_protocol.cpp
        tests/test_session_manager.cpp
        tests/test_gateway.cpp
        tests/test_skill_loader.cpp
        tests/test_memory_manager.cpp
        tests/test_agent_loop.cpp
        tests/test_mcp_server.cpp
        tests/test_mcp_integration.cpp
        tests/test_openai_provider.cpp
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

    include(GoogleTest)
    gtest_discover_tests(quantclaw_tests)
endif()
