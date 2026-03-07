// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/security/rbac.hpp"
#include "quantclaw/gateway/protocol.hpp"

namespace quantclaw {

Role RoleFromString(const std::string& s) {
    if (s == "viewer") return Role::kViewer;
    if (s == "node") return Role::kNode;
    return Role::kOperator;
}

std::string RoleToString(Role r) {
    switch (r) {
        case Role::kOperator: return "operator";
        case Role::kViewer:   return "viewer";
        case Role::kNode:     return "node";
    }
    return "operator";
}

std::vector<std::string> DefaultScopes(Role role) {
    switch (role) {
        case Role::kOperator:
            return {scopes::kOperatorRead, scopes::kOperatorWrite, scopes::kOperatorAdmin};
        case Role::kViewer:
            return {scopes::kOperatorRead};
        case Role::kNode:
            return {scopes::kNodeExecute, scopes::kNodeRead};
    }
    return {};
}

RBACChecker::RBACChecker() {
    init_default_rules();
}

void RBACChecker::init_default_rules() {
    namespace m = gateway::methods;

    // Read-only methods: any read scope
    for (const char* method : {
        m::kGatewayHealth, m::kGatewayStatus, m::kConfigGet,
        m::kSessionsList, m::kSessionsHistory,
        m::kChannelsList, m::kChannelsStatus,
        m::kOcHealth, m::kOcStatus, m::kOcModelsList,
        m::kOcSessionsPreview, m::kOcChatHistory,
        m::kSkillsStatus, m::kExecApprovals,
        m::kCronList, m::kCronRuns,
        m::kMemoryStatus, m::kMemorySearch,
        m::kPluginsList, m::kPluginsTools, m::kPluginsServices,
        m::kPluginsProviders, m::kPluginsCommands, m::kPluginsGateway,
        m::kQueueStatus
    }) {
        method_scopes_[method] = {
            scopes::kOperatorRead, scopes::kOperatorWrite,
            scopes::kOperatorAdmin, scopes::kNodeRead, scopes::kNodeExecute
        };
    }

    // Write methods: operator.write or operator.admin
    for (const char* method : {
        m::kConfigSet, m::kConfigReload,
        m::kSessionsDelete, m::kSessionsReset, m::kSessionsPatch, m::kSessionsCompact,
        m::kModelsSet, m::kSkillsInstall,
        m::kCronAdd, m::kCronRemove, m::kCronUpdate, m::kCronRun,
        m::kExecApprovalReq,
        m::kPluginsCallTool,
        m::kQueueConfigure, m::kQueueCancel, m::kQueueAbort
    }) {
        method_scopes_[method] = {
            scopes::kOperatorWrite, scopes::kOperatorAdmin
        };
    }

    // Agent execution: operator.write or node.execute
    for (const char* method : {
        m::kAgentRequest, m::kAgentStop,
        m::kOcChatSend, m::kOcChatAbort,
        m::kChainExecute
    }) {
        method_scopes_[method] = {
            scopes::kOperatorWrite, scopes::kOperatorAdmin, scopes::kNodeExecute
        };
    }

    // UI compat methods (no constants in protocol.hpp): read scope
    const std::unordered_set<std::string> read_scopes{
        scopes::kOperatorRead, scopes::kOperatorWrite,
        scopes::kOperatorAdmin, scopes::kNodeRead, scopes::kNodeExecute
    };
    for (const auto& method : std::vector<std::string>{
        "agent.identity.get", "node.list", "device.pair.list",
        "logs.tail", "usage.cost", "sessions.usage",
        "sessions.usage.timeseries", "sessions.usage.logs",
        "cron.status", "config.schema", "agents.list"
    }) {
        method_scopes_[method] = read_scopes;
    }

    // Connect: always allowed (auth check is separate)
    method_scopes_[m::kConnectHello] = {};
    method_scopes_[m::kOcConnect] = {};
    method_scopes_[m::kOcToolsCatalog] = {
        scopes::kOperatorRead, scopes::kOperatorWrite,
        scopes::kOperatorAdmin, scopes::kNodeRead, scopes::kNodeExecute
    };
}

bool RBACChecker::IsAllowed(const std::string& method,
                              const std::string& /*role*/,
                              const std::vector<std::string>& client_scopes) const {
    auto it = method_scopes_.find(method);
    if (it == method_scopes_.end()) {
        // Unknown method: require operator.admin
        for (const auto& s : client_scopes) {
            if (s == scopes::kOperatorAdmin) return true;
        }
        return false;
    }

    // Empty required set = always allowed
    if (it->second.empty()) return true;

    // Check if any client scope matches required scopes
    for (const auto& s : client_scopes) {
        if (it->second.count(s)) return true;
    }
    return false;
}

std::vector<std::string> RBACChecker::RequiredScopes(const std::string& method) const {
    auto it = method_scopes_.find(method);
    if (it == method_scopes_.end()) return {scopes::kOperatorAdmin};
    return std::vector<std::string>(it->second.begin(), it->second.end());
}

}  // namespace quantclaw
