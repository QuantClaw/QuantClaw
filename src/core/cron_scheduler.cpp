// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

#include "quantclaw/core/cron_scheduler.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace quantclaw {

// --- CronJob ---

nlohmann::json CronJob::ToJson() const {
  nlohmann::json j;
  j["id"] = id;
  j["name"] = name;
  j["schedule"] = schedule;
  j["message"] = message;
  j["sessionKey"] = session_key;
  j["enabled"] = enabled;

  auto to_iso = [](std::chrono::system_clock::time_point tp) -> std::string {
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm;
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
  };

  if (last_run.time_since_epoch().count() > 0) {
    j["lastRun"] = to_iso(last_run);
  }
  if (next_run.time_since_epoch().count() > 0) {
    j["nextRun"] = to_iso(next_run);
  }
  return j;
}

CronJob CronJob::FromJson(const nlohmann::json& j) {
  CronJob job;
  job.id = j.value("id", "");
  job.name = j.value("name", "");
  job.schedule = j.value("schedule", "");
  job.message = j.value("message", "");
  job.session_key = j.value("sessionKey", "agent:main:main");
  job.enabled = j.value("enabled", true);
  return job;
}

// --- CronExpression ---

CronExpression::CronExpression(const std::string& expr) {
  std::istringstream ss(expr);
  std::string fields[5];
  for (int i = 0; i < 5; ++i) {
    if (!(ss >> fields[i])) {
      fields[i] = "*";
    }
  }

  minute_ = parse_field(fields[0], 0, 59);
  hour_ = parse_field(fields[1], 0, 23);
  day_of_month_ = parse_field(fields[2], 1, 31);
  month_ = parse_field(fields[3], 1, 12);
  day_of_week_ = parse_field(fields[4], 0, 6);
}

CronExpression::Field CronExpression::parse_field(const std::string& field,
                                                  int min, int max) {
  Field f;

  if (field == "*")
    return f;  // empty = wildcard

  // Handle */N (step)
  if (field.size() > 2 && field.substr(0, 2) == "*/") {
    int step = std::stoi(field.substr(2));
    if (step > 0) {
      for (int i = min; i <= max; i += step) {
        f.values.push_back(i);
      }
    }
    return f;
  }

  // Handle comma-separated and ranges: "1,5,10" or "1-5" or "1-5,10"
  std::istringstream ss(field);
  std::string token;
  while (std::getline(ss, token, ',')) {
    auto dash = token.find('-');
    if (dash != std::string::npos) {
      int lo = std::stoi(token.substr(0, dash));
      int hi = std::stoi(token.substr(dash + 1));
      for (int i = lo; i <= hi && i <= max; ++i) {
        f.values.push_back(i);
      }
    } else {
      f.values.push_back(std::stoi(token));
    }
  }

  return f;
}

bool CronExpression::field_matches(const Field& f, int value) {
  if (f.values.empty())
    return true;  // wildcard
  return std::find(f.values.begin(), f.values.end(), value) != f.values.end();
}

bool CronExpression::Matches(const std::tm& tm) const {
  return field_matches(minute_, tm.tm_min) &&
         field_matches(hour_, tm.tm_hour) &&
         field_matches(day_of_month_, tm.tm_mday) &&
         field_matches(month_, tm.tm_mon + 1) &&
         field_matches(day_of_week_, tm.tm_wday);
}

std::chrono::system_clock::time_point
CronExpression::NextAfter(std::chrono::system_clock::time_point after) const {
  // Start from next minute
  auto t = std::chrono::system_clock::to_time_t(after);
  std::tm tm;
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  tm.tm_sec = 0;
  tm.tm_min += 1;
  std::mktime(&tm);  // normalize

  // Search up to 366 days ahead
  for (int i = 0; i < 525960; ++i) {  // 366 * 24 * 60
    if (Matches(tm)) {
      return std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }
    tm.tm_min += 1;
    std::mktime(&tm);
  }

  // Fallback: 1 hour from now
  return after + std::chrono::hours(1);
}

// --- CronScheduler ---

CronScheduler::CronScheduler(std::shared_ptr<spdlog::logger> logger)
    : logger_(std::move(logger)) {}

CronScheduler::~CronScheduler() {
  Stop();
}

void CronScheduler::Load(const std::string& filepath) {
  storage_path_ = filepath;

  if (!std::filesystem::exists(filepath))
    return;

  try {
    std::ifstream ifs(filepath);
    nlohmann::json j;
    ifs >> j;

    std::lock_guard<std::mutex> lock(mu_);
    jobs_.clear();
    if (j.is_array()) {
      for (const auto& item : j) {
        auto job = CronJob::FromJson(item);
        if (!job.id.empty() && !job.schedule.empty()) {
          CronExpression expr(job.schedule);
          job.next_run = expr.NextAfter(std::chrono::system_clock::now());
          jobs_.push_back(std::move(job));
        }
      }
    }
    logger_->info("Loaded {} cron jobs from {}", jobs_.size(), filepath);
  } catch (const std::exception& e) {
    logger_->error("Failed to load cron jobs: {}", e.what());
  }
}

void CronScheduler::Save(const std::string& filepath) const {
  std::lock_guard<std::mutex> lock(mu_);

  nlohmann::json arr = nlohmann::json::array();
  for (const auto& job : jobs_) {
    arr.push_back(job.ToJson());
  }

  auto parent = std::filesystem::path(filepath).parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }

  std::ofstream ofs(filepath);
  ofs << arr.dump(2) << std::endl;
}

std::string CronScheduler::AddJob(const std::string& name,
                                  const std::string& schedule,
                                  const std::string& message,
                                  const std::string& session_key) {
  // Validate cron expression
  CronExpression expr(schedule);

  CronJob job;
  job.id = generate_id();
  job.name = name;
  job.schedule = schedule;
  job.message = message;
  job.session_key = session_key;
  job.next_run = expr.NextAfter(std::chrono::system_clock::now());

  std::string id = job.id;

  {
    std::lock_guard<std::mutex> lock(mu_);
    jobs_.push_back(std::move(job));
  }

  if (!storage_path_.empty())
    Save(storage_path_);

  logger_->info("Added cron job '{}' ({}): {}", name, id, schedule);
  return id;
}

bool CronScheduler::RemoveJob(const std::string& id) {
  if (id.empty())
    return false;  // Guard against empty id

  std::lock_guard<std::mutex> lock(mu_);

  // Find jobs matching id (exact match or unambiguous prefix match)
  std::vector<size_t> matches;
  for (size_t i = 0; i < jobs_.size(); ++i) {
    if (jobs_[i].id == id) {
      // Exact match found, remove this job immediately
      jobs_.erase(jobs_.begin() + i);
      if (!storage_path_.empty()) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& j : jobs_)
          arr.push_back(j.ToJson());
        std::ofstream ofs(storage_path_);
        ofs << arr.dump(2) << std::endl;
      }
      return true;
    }
    // Check for prefix match
    if (id.size() < jobs_[i].id.size() &&
        jobs_[i].id.substr(0, id.size()) == id) {
      matches.push_back(i);
    }
  }

  // Only allow prefix deletion if unambiguous (exactly one match)
  if (matches.size() != 1)
    return false;

  jobs_.erase(jobs_.begin() + matches[0]);

  if (!storage_path_.empty()) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& j : jobs_)
      arr.push_back(j.ToJson());
    std::ofstream ofs(storage_path_);
    ofs << arr.dump(2) << std::endl;
  }

  return true;
}

std::vector<CronJob> CronScheduler::ListJobs() const {
  std::lock_guard<std::mutex> lock(mu_);
  return jobs_;
}

void CronScheduler::Start(JobHandler handler) {
  if (running_)
    return;

  handler_ = std::move(handler);
  running_ = true;
  thread_ = std::thread([this] { scheduler_loop(); });
}

void CronScheduler::Stop() {
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
}

void CronScheduler::scheduler_loop() {
  while (running_) {
    auto now = std::chrono::system_clock::now();

    std::vector<CronJob*> due_jobs;
    {
      std::lock_guard<std::mutex> lock(mu_);
      for (auto& job : jobs_) {
        if (!job.enabled)
          continue;
        if (job.next_run <= now) {
          due_jobs.push_back(&job);
        }
      }
    }

    for (auto* job : due_jobs) {
      if (handler_) {
        try {
          handler_(*job);
        } catch (const std::exception& e) {
          logger_->error("Cron job '{}' failed: {}", job->name, e.what());
        }
      }

      // Update last_run and compute next_run
      job->last_run = now;
      CronExpression expr(job->schedule);
      job->next_run = expr.NextAfter(now);
    }

    if (!due_jobs.empty() && !storage_path_.empty()) {
      Save(storage_path_);
    }

    // Sleep 30 seconds between checks
    for (int i = 0; i < 30 && running_; ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}

std::string CronScheduler::generate_id() const {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uint32_t> dist;
  std::ostringstream ss;
  ss << std::hex << dist(gen) << dist(gen);
  return ss.str().substr(0, 12);
}

}  // namespace quantclaw
