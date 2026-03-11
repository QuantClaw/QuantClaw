// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace quantclaw {

struct CronJob {
  std::string id;
  std::string name;
  std::string schedule;     // cron expression (e.g. "*/5 * * * *")
  std::string message;      // message to send to agent
  std::string session_key;  // session to use
  bool enabled = true;
  std::chrono::system_clock::time_point last_run;
  std::chrono::system_clock::time_point next_run;

  nlohmann::json ToJson() const;
  static CronJob FromJson(const nlohmann::json& j);
};

// Simple cron expression evaluator supporting:
//   minute hour day-of-month month day-of-week
// Supports: *, */N, N, N-M, N,M,O
class CronExpression {
 public:
  explicit CronExpression(const std::string& expr);

  // Check if the given time matches the cron expression
  bool Matches(const std::tm& tm) const;

  // Calculate next run time after the given time
  std::chrono::system_clock::time_point
  NextAfter(std::chrono::system_clock::time_point after) const;

 private:
  struct Field {
    std::vector<int> values;  // empty = wildcard (all)
  };

  Field minute_;
  Field hour_;
  Field day_of_month_;
  Field month_;
  Field day_of_week_;

  static Field parse_field(const std::string& field, int min, int max);
  static bool field_matches(const Field& f, int value);
};

// Manages persistent cron jobs with a tick-based scheduler.
class CronScheduler {
 public:
  using JobHandler = std::function<void(const CronJob&)>;

  explicit CronScheduler(std::shared_ptr<spdlog::logger> logger);
  ~CronScheduler();

  // Load jobs from persistent storage
  void Load(const std::string& filepath);

  // Save jobs to persistent storage
  void Save(const std::string& filepath) const;

  // Add a new cron job
  std::string AddJob(const std::string& name, const std::string& schedule,
                     const std::string& message,
                     const std::string& session_key = "agent:main:main");

  // Remove a job by ID
  bool RemoveJob(const std::string& id);

  // List all jobs
  std::vector<CronJob> ListJobs() const;

  // Start the scheduler loop
  void Start(JobHandler handler);

  // Stop the scheduler
  void Stop();

  bool IsRunning() const {
    return running_;
  }

 private:
  void scheduler_loop();
  std::string generate_id() const;

  std::shared_ptr<spdlog::logger> logger_;
  mutable std::mutex mu_;
  std::vector<CronJob> jobs_;
  JobHandler handler_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::string storage_path_;
};

}  // namespace quantclaw
