// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/rpchandler/rpc_handler_cron.hpp"

#include "quantclaw/core/agent_loop.hpp"
#include "quantclaw/core/cron_scheduler.hpp"
#include "quantclaw/core/prompt_builder.hpp"
#include "quantclaw/session/session_manager.hpp"
#include <chrono>

namespace quantclaw::gateway {

CronHandler::CronHandler(const HandlerContext& ctx)
    : ctx_(ctx) {}

void CronHandler::RegisterHandlers(GatewayServer& /*server*/) {
  if (!ctx_.cron_scheduler) return;

  // --- cron.list ---
  ctx_.server->RegisterHandler(methods::kCronList,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleCronList(params, client);
      });

  // --- cron.add ---
  ctx_.server->RegisterHandler(methods::kCronAdd,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleCronAdd(params, client);
      });

  // --- cron.remove ---
  ctx_.server->RegisterHandler(methods::kCronRemove,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleCronRemove(params, client);
      });

  // --- cron.update ---
  ctx_.server->RegisterHandler(methods::kCronUpdate,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleCronUpdate(params, client);
      });

  // --- cron.run ---
  ctx_.server->RegisterHandler(methods::kCronRun,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleCronRun(params, client);
      });

  // --- cron.runs ---
  ctx_.server->RegisterHandler(methods::kCronRuns,
      [this](const nlohmann::json& params, ClientConnection& client) {
        return HandleCronRuns(params, client);
      });
}

nlohmann::json CronHandler::HandleCronList(const nlohmann::json& params, ClientConnection& /*client*/) {
  int limit = params.value("limit", 0);
  int offset = params.value("offset", 0);
  auto jobs = ctx_.cron_scheduler->ListJobs();
  int total = static_cast<int>(jobs.size());
  int start = std::clamp(offset, 0, total);
  int end = (limit > 0) ? std::min(start + limit, total) : total;

  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();

  auto tp_to_ms = [](std::chrono::system_clock::time_point tp) -> long long {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               tp.time_since_epoch())
        .count();
  };

  nlohmann::json job_list = nlohmann::json::array();
  for (size_t i = static_cast<size_t>(start);
       i < static_cast<size_t>(end); ++i) {
    const auto& job = jobs[i];
    long long last_ms = tp_to_ms(job.last_run);
    long long next_ms = tp_to_ms(job.next_run);

    nlohmann::json state = nlohmann::json::object();
    if (next_ms > 0)
      state["nextRunAtMs"] = next_ms;
    if (last_ms > 0)
      state["lastRunAtMs"] = last_ms;

    job_list.push_back(
        {{"id", job.id},
         {"name", job.name},
         {"description", ""},
         {"enabled", job.enabled},
         {"deleteAfterRun", false},
         {"createdAtMs", now_ms},
         {"updatedAtMs", now_ms},
         {"schedule", {{"kind", "cron"}, {"expr", job.schedule}}},
         {"sessionTarget", "main"},
         {"wakeMode", "now"},
         {"payload", {{"kind", "agentTurn"}, {"message", job.message}}},
         {"state", state}});
  }

  bool has_more = end < total;
  return {{"jobs", job_list},
          {"total", total},
          {"offset", offset},
          {"limit", limit > 0 ? limit : total},
          {"hasMore", has_more},
          {"nextOffset",
           has_more ? nlohmann::json(end) : nlohmann::json(nullptr)}};
}

nlohmann::json CronHandler::HandleCronAdd(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string name = params.value("name", "");
  std::string session_key = params.value("sessionKey", "agent:main:main");

  // Extract schedule expression (flat or nested)
  std::string schedule;
  if (params.contains("schedule") && params["schedule"].is_object()) {
    const auto& sched = params["schedule"];
    std::string kind = sched.value("kind", "cron");
    if (kind == "cron") {
      schedule = sched.value("expr", "");
    } else if (kind == "every") {
      long long every_ms = sched.value("everyMs", 3600000LL);
      if (every_ms <= 0 || every_ms % 60000LL != 0) {
        throw std::runtime_error(
            "everyMs must be a positive multiple of 60000");
      }
      long long every_min = every_ms / 60000LL;
      if (every_min < 60) {
        schedule = "*/" + std::to_string(every_min) + " * * * *";
      } else if (every_min % 60 == 0) {
        long long every_hr = every_min / 60;
        schedule = "0 */" + std::to_string(every_hr) + " * * *";
      } else {
        throw std::runtime_error(
            "everyMs of " + std::to_string(every_ms) +
            " ms (" + std::to_string(every_min) +
            " min) cannot be represented in cron; "
            "sub-hour values must be < 3600000 ms, "
            "hourly values must be a multiple of 3600000 ms");
      }
    } else if (kind == "at") {
      schedule = sched.value("at", "0 * * * *");
    }
  } else {
    schedule = params.value("schedule", "");
  }

  // Extract message (flat or nested in payload)
  std::string message;
  if (params.contains("payload") && params["payload"].is_object()) {
    message = params["payload"].value("message", "");
  } else {
    message = params.value("message", "");
  }

  if (name.empty() && !message.empty()) {
    name = message.substr(0, std::min(message.size(), (size_t)40));
  }
  if (schedule.empty() || message.empty()) {
    throw std::runtime_error("schedule and message are required");
  }

  auto id = ctx_.cron_scheduler->AddJob(name, schedule, message, session_key);
  return {{"ok", true}, {"id", id}};
}

nlohmann::json CronHandler::HandleCronRemove(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string id = params.value("id", "");
  if (id.empty()) {
    throw std::runtime_error("cron job id is required");
  }
  bool removed = ctx_.cron_scheduler->RemoveJob(id);
  return {{"ok", removed}};
}

nlohmann::json CronHandler::HandleCronUpdate(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string id = params.value("id", "");
  if (id.empty()) {
    throw std::runtime_error("cron job id is required");
  }

  // Flatten nested patch object if present
  nlohmann::json flat = params;
  if (params.contains("patch") && params["patch"].is_object()) {
    for (auto& [k, v] : params["patch"].items()) {
      flat[k] = v;
    }
  }

  auto jobs = ctx_.cron_scheduler->ListJobs();
  for (const auto& job : jobs) {
    if (job.id == id) {
      std::string name = flat.value("name", job.name);
      std::string schedule = job.schedule;
      std::string message = job.message;

      // Extract schedule from nested or flat
      if (flat.contains("schedule") && flat["schedule"].is_object()) {
        const auto& s = flat["schedule"];
        if (s.value("kind", "") == "cron") {
          schedule = s.value("expr", job.schedule);
        }
      } else if (flat.contains("schedule") && flat["schedule"].is_string()) {
        schedule = flat["schedule"].get<std::string>();
      }

      // Extract message from nested payload or flat
      if (flat.contains("payload") && flat["payload"].is_object()) {
        message = flat["payload"].value("message", job.message);
      } else if (flat.contains("message")) {
        message = flat.value("message", job.message);
      }

      bool updated = ctx_.cron_scheduler->UpdateJob(job.id, name, schedule, message, job.session_key);
      if (!updated)
        throw std::runtime_error("failed to update cron job: " + id);
      return {{"ok", true}, {"id", job.id}};
    }
  }

  throw std::runtime_error("cron job not found: " + id);
}

nlohmann::json CronHandler::HandleCronRun(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string id = params.value("id", "");
  if (id.empty()) {
    throw std::runtime_error("cron job id is required");
  }

  auto jobs = ctx_.cron_scheduler->ListJobs();

  // Exact match takes priority; otherwise require unambiguous prefix
  const CronJob* matched = nullptr;
  std::vector<const CronJob*> prefix_matches;
  for (const auto& job : jobs) {
    if (job.id == id) {
      matched = &job;
      break;
    }
    if (id.size() < job.id.size() &&
        job.id.substr(0, id.size()) == id) {
      prefix_matches.push_back(&job);
    }
  }

  if (!matched) {
    if (prefix_matches.size() == 1) {
      matched = prefix_matches[0];
    } else if (prefix_matches.empty()) {
      throw std::runtime_error("cron job not found: " + id);
    } else {
      throw std::runtime_error("ambiguous cron job id: " + id +
                               " matches " +
                               std::to_string(prefix_matches.size()) +
                               " jobs");
    }
  }

  const auto& job = *matched;
  auto session = ctx_.session_manager->GetOrCreate(job.session_key, job.name, "cron");
  auto history_msgs = ctx_.session_manager->GetHistory(job.session_key);

  std::vector<quantclaw::Message> history;
  quantclaw::Message msg;
  for (const auto& m : history_msgs) {
    msg = {};
    msg.role = m.role;
    msg.content = m.content;
    history.push_back(msg);
  }

  auto system_prompt = ctx_.prompt_builder->BuildFull(job.session_key);
  auto new_msgs = ctx_.agent_loop->ProcessMessage(job.message, history, system_prompt);

  // Persist the triggering user message before assistant/tool replies
  quantclaw::SessionMessage sm;
  sm.role = "user";
  sm.content.push_back(quantclaw::ContentBlock::MakeText(job.message));
  ctx_.session_manager->AppendMessage(job.session_key, sm);

  for (const auto& msg : new_msgs) {
    sm = {};
    sm.role = msg.role;
    sm.content = msg.content;
    ctx_.session_manager->AppendMessage(job.session_key, sm);
  }

  nlohmann::json r;
  r["ok"] = true;
  r["jobId"] = job.id;
  r["messagesGenerated"] = static_cast<int>(new_msgs.size());
  return r;
}

nlohmann::json CronHandler::HandleCronRuns(const nlohmann::json& params, ClientConnection& /*client*/) {
  std::string filter_id = params.value("id", "");
  int limit = params.value("limit", 0);
  int offset = params.value("offset", 0);
  auto jobs = ctx_.cron_scheduler->ListJobs();

  auto tp_to_ms = [](std::chrono::system_clock::time_point tp) -> long long {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               tp.time_since_epoch())
        .count();
  };

  nlohmann::json all_entries = nlohmann::json::array();
  for (const auto& job : jobs) {
    if (!filter_id.empty() && job.id != filter_id)
      continue;
    long long last_ms = tp_to_ms(job.last_run);
    long long next_ms = tp_to_ms(job.next_run);
    if (last_ms > 0) {
      all_entries.push_back(
          {{"ts", last_ms},
           {"jobId", job.id},
           {"jobName", job.name},
           {"status", "ok"},
           {"runAtMs", last_ms},
           {"nextRunAtMs", next_ms > 0 ? nlohmann::json(next_ms)
                                       : nlohmann::json(nullptr)}});
    }
  }

  int total = static_cast<int>(all_entries.size());
  int start = std::clamp(offset, 0, total);
  int end = (limit > 0) ? std::min(start + limit, total) : total;

  nlohmann::json entries = nlohmann::json::array();
  for (size_t i = static_cast<size_t>(start);
       i < static_cast<size_t>(end); ++i)
    entries.push_back(all_entries[i]);

  bool has_more = end < total;
  return {{"entries", entries},
          {"total", total},
          {"offset", offset},
          {"limit", limit > 0 ? limit : total},
          {"hasMore", has_more},
          {"nextOffset",
           has_more ? nlohmann::json(end) : nlohmann::json(nullptr)}};
}

}  // namespace quantclaw::gateway
