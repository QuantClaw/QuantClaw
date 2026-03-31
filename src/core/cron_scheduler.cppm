// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>

export module quantclaw.core.cron_scheduler;

import std;
import nlohmann.json;

export namespace quantclaw {

struct CronJob {
  std::string id;
  std::string name;
  std::string schedule;
  std::string message;
  std::string session_key;
  bool enabled = true;
  std::chrono::system_clock::time_point last_run;
  std::chrono::system_clock::time_point next_run;

  nlohmann::json ToJson() const;
  static CronJob FromJson(const nlohmann::json& j);
};

class CronExpression {
 public:
  explicit CronExpression(const std::string& expr);
  bool Matches(const std::tm& tm) const;
  std::chrono::system_clock::time_point
  NextAfter(std::chrono::system_clock::time_point after) const;

 private:
  struct Field {
    std::vector<int> values;
  };

  Field minute_;
  Field hour_;
  Field day_of_month_;
  Field month_;
  Field day_of_week_;

  static Field parse_field(const std::string& field, int min, int max);
  static bool field_matches(const Field& f, int value);
};

class CronScheduler {
 public:
  using JobHandler = std::function<void(const CronJob&)>;

  explicit CronScheduler(std::shared_ptr<spdlog::logger> logger);
  ~CronScheduler();

  void Load(const std::string& filepath);
  void Save(const std::string& filepath) const;
  std::string AddJob(const std::string& name, const std::string& schedule,
                     const std::string& message,
                     const std::string& session_key = "agent:main:main");
  bool RemoveJob(const std::string& id);
  std::vector<CronJob> ListJobs() const;
  void Start(JobHandler handler);
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