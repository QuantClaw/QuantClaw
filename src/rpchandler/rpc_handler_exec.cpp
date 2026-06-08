// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/rpchandler/rpc_handler_exec.hpp"
#include "quantclaw/security/exec_approval.hpp"

namespace quantclaw::gateway {

ExecApprovalHandler::ExecApprovalHandler(const HandlerContext& ctx)
    : ctx_(ctx) {}

void ExecApprovalHandler::RegisterHandlers(GatewayServer& /*server*/) {
  if (!ctx_.exec_approval_mgr) return;

  // --- exec.approval.request ---
  ctx_.server->RegisterHandler(methods::kExecApprovalReq,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleExecApprovalRequest(params, client);
      });

  // --- exec.approvals.get ---
  ctx_.server->RegisterHandler(methods::kExecApprovals,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleExecApprovalsGet(params, client);
      });
}

nlohmann::json ExecApprovalHandler::HandleExecApprovalRequest(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string command = params.value("command", "");
  if (command.empty()) {
    throw std::runtime_error("command is required");
  }

  std::string cwd = params.value("cwd", "");
  std::string agent_id = params.value("agentId", "");
  std::string session_key = params.value("sessionKey", "");

  auto decision = ctx_.exec_approval_mgr->RequestApproval(command, cwd, agent_id, session_key);

  std::string decision_str;
  switch (decision) {
    case quantclaw::ApprovalDecision::kApproved:
      decision_str = "approved";
      break;
    case quantclaw::ApprovalDecision::kDenied:
      decision_str = "denied";
      break;
    case quantclaw::ApprovalDecision::kPending:
      decision_str = "pending";
      break;
    default:
      decision_str = "timeout";
      break;
  }

  return {{"decision", decision_str}};
}

nlohmann::json ExecApprovalHandler::HandleExecApprovalsGet(const nlohmann::json& /*params*/, ClientConnection& /*client*/) {
  const auto& cfg = ctx_.exec_approval_mgr->GetConfig();

  std::string mode_str;
  switch (cfg.ask) {
    case quantclaw::AskMode::kOff:
      mode_str = "off";
      break;
    case quantclaw::AskMode::kOnMiss:
      mode_str = "on-miss";
      break;
    case quantclaw::AskMode::kAlways:
      mode_str = "always";
      break;
  }

  nlohmann::json patterns = nlohmann::json::array();
  for (const auto& p : cfg.allowlist) {
    patterns.push_back(p);
  }

  auto pending = ctx_.exec_approval_mgr->PendingRequests();
  nlohmann::json pending_json = nlohmann::json::array();
  for (const auto& req : pending) {
    pending_json.push_back({{"id", req.id},
                            {"command", req.command},
                            {"cwd", req.cwd},
                            {"agentId", req.agent_id},
                            {"sessionKey", req.session_key}});
  }

  return {{"mode", mode_str},
          {"allowlist", patterns},
          {"pending", pending_json}};
}

}  // namespace quantclaw::gateway
