// Copyright 2025 QuantClaw Contributors
// SPDX-License-Identifier: Apache-2.0

module;

#include <spdlog/spdlog.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

module quantclaw.core.memory_manager;

import std;

namespace quantclaw {

MemoryManager::MemoryManager(const std::filesystem::path& workspace_path,
                             std::shared_ptr<spdlog::logger> logger)
    : workspace_path_(workspace_path), logger_(logger) {
  // Determine base dir from workspace path
  // Expected: ~/.quantclaw/agents/{agentId}/workspace
  // Or legacy: ~/.quantclaw/workspace
  if (workspace_path_.generic_string().find("/agents/") != std::string::npos) {
    // New layout: base_dir is 3 levels up from workspace
    base_dir_ = workspace_path_.parent_path().parent_path().parent_path();
  } else {
    // Legacy layout: base_dir is one level up
    base_dir_ = workspace_path_.parent_path();
  }

  std::filesystem::create_directories(workspace_path_);
  logger_->info("MemoryManager initialized with workspace: {}",
                workspace_path_.string());
}

MemoryManager::~MemoryManager() {
  StopFileWatcher();
}

void MemoryManager::LoadWorkspaceFiles() {
  logger_->info("Loading workspace files from: {}", workspace_path_.string());

  for (const auto& name :
       {"SOUL.md", "USER.md", "MEMORY.md", "AGENTS.md", "TOOLS.md"}) {
    try {
      auto content = ReadIdentityFile(name);
      if (!content.empty()) {
        logger_->debug("Loaded {} ({} bytes)", name, content.size());
      }
    } catch (const std::exception& e) {
      logger_->debug("No {} found: {}", name, e.what());
    }
  }

  // Load daily memory files
  auto memory_dir = workspace_path_ / "memory";
  if (std::filesystem::exists(memory_dir)) {
    int loaded_count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(memory_dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".md") {
        try {
          auto content = read_file_content(entry.path());
          loaded_count++;
        } catch (const std::exception& e) {
          logger_->warn("Failed to load memory file {}: {}",
                        entry.path().filename().string(), e.what());
        }
      }
    }
    logger_->info("Loaded {} daily memory files", loaded_count);
  }

  logger_->info("Workspace files loaded successfully");
  rebuild_search_index();
}

std::string MemoryManager::ReadIdentityFile(const std::string& filename) const {
  auto filepath = workspace_path_ / filename;
  return read_file_content(filepath);
}

std::string MemoryManager::ReadAgentsFile() const {
  return ReadIdentityFile("AGENTS.md");
}

std::string MemoryManager::ReadToolsFile() const {
  return ReadIdentityFile("TOOLS.md");
}

// Tokenize text into lowercase alphanumeric words (same convention as the
// MMR reranker) without heap-allocating a full token collection.
static void tokenize_into(std::string_view text,
                          std::unordered_set<std::string>& out) {
  std::size_t start = std::string_view::npos;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    bool alnum =
        i < text.size() && std::isalnum(static_cast<unsigned char>(text[i]));
    if (alnum) {
      if (start == std::string_view::npos)
        start = i;
    } else if (start != std::string_view::npos) {
      std::string tok(text.substr(start, i - start));
      for (char& c : tok)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      out.insert(std::move(tok));
      start = std::string_view::npos;
    }
  }
}

void MemoryManager::rebuild_search_index() {
  std::unordered_map<std::string, std::unordered_set<std::string>> new_index;

  auto index_file = [&](const std::string& label,
                        const std::filesystem::path& path) {
    try {
      auto content = read_file_content(path);
      std::unordered_set<std::string> tokens;
      tokenize_into(content, tokens);
      for (auto& tok : tokens) {
        new_index[tok].insert(label);
      }
    } catch (const std::exception&) {}
  };

  for (const auto& name :
       {"SOUL.md", "USER.md", "MEMORY.md", "AGENTS.md", "TOOLS.md"}) {
    index_file(name, workspace_path_ / name);
  }

  auto memory_dir = workspace_path_ / "memory";
  if (std::filesystem::exists(memory_dir)) {
    for (const auto& entry : std::filesystem::directory_iterator(memory_dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".md") {
        index_file(entry.path().string(), entry.path());
      }
    }
  }

  std::unique_lock<std::shared_mutex> lock(cache_mutex_);
  token_index_ = std::move(new_index);
}

std::vector<std::string>
MemoryManager::SearchMemory(const std::string& query) const {
  // Tokenize the query and look up each token in the inverted index.
  // Files that match all query tokens are returned (AND semantics).
  std::unordered_set<std::string> query_tokens;
  tokenize_into(query, query_tokens);

  if (query_tokens.empty()) {
    return {};
  }

  // Candidate files: start with the set for the first token, intersect the rest
  std::unordered_set<std::string> candidates;
  {
    std::shared_lock<std::shared_mutex> lock(cache_mutex_);
    bool first = true;
    for (const auto& tok : query_tokens) {
      auto it = token_index_.find(tok);
      if (it == token_index_.end()) {
        // Token not found — no file can satisfy all tokens
        return {};
      }
      if (first) {
        candidates = it->second;
        first = false;
      } else {
        std::unordered_set<std::string> intersection;
        for (const auto& f : candidates) {
          if (it->second.count(f))
            intersection.insert(f);
        }
        candidates = std::move(intersection);
      }
      if (candidates.empty())
        return {};
    }
  }

  // Read and return matched files
  std::vector<std::string> results;
  results.reserve(candidates.size());
  for (const auto& label : candidates) {
    try {
      // label is either a bare filename (identity files) or an absolute path
      std::filesystem::path path =
          std::filesystem::path(label).is_absolute()
              ? std::filesystem::path(label)
              : workspace_path_ / label;
      auto content = read_file_content(path);
      results.push_back("File: " + label + "\nContent: " + content);
    } catch (const std::exception&) {}
  }
  return results;
}

void MemoryManager::SaveDailyMemory(const std::string& content) {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::tm tm;
#ifdef _WIN32
  localtime_s(&tm, &time_t);
#else
  localtime_r(&time_t, &tm);
#endif

  std::ostringstream date_stream;
  date_stream << std::put_time(&tm, "%Y-%m-%d");
  auto date_str = date_stream.str();

  auto memory_dir = workspace_path_ / "memory";
  std::filesystem::create_directories(memory_dir);

  std::ostringstream entry_stream;
  entry_stream << "## " << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "\n";
  entry_stream << content << "\n";
  auto entry_content = entry_stream.str();

  auto memory_file = memory_dir / (date_str + ".md");
  std::ofstream file(memory_file, std::ios::app);
  if (file.is_open()) {
    file << entry_content;
    logger_->debug("Saved memory entry to {}", memory_file.string());
  } else {
    logger_->error("Failed to save memory entry to {}", memory_file.string());
    throw std::runtime_error("Failed to write to memory file: " +
                             memory_file.string());
  }
}

void MemoryManager::StartFileWatcher() {
  if (watching_) {
    return;
  }

  // Create self-pipe for clean shutdown signalling
  if (::pipe2(wakeup_pipe_, O_CLOEXEC) != 0) {
    logger_->error("Failed to create wakeup pipe for file watcher: {}",
                   std::strerror(errno));
    return;
  }

  watching_ = true;

  watcher_thread_ = std::make_unique<std::thread>([this]() {
    // Set up inotify instance watching the workspace directory
    int ifd = ::inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (ifd < 0) {
      logger_->error("inotify_init1 failed: {}", std::strerror(errno));
      return;
    }

    int wd = ::inotify_add_watch(
        ifd, workspace_path_.c_str(),
        IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
    if (wd < 0) {
      logger_->error("inotify_add_watch failed for {}: {}",
                     workspace_path_.string(), std::strerror(errno));
      ::close(ifd);
      return;
    }

    // Watched identity filenames for fast lookup
    static constexpr std::array<std::string_view, 5> kWatched{
        "SOUL.md", "USER.md", "MEMORY.md", "AGENTS.md", "TOOLS.md"};

    // poll(2) monitors both the inotify fd and the read end of the wake pipe
    std::array<::pollfd, 2> pfds{};
    pfds[0] = {ifd,              POLLIN, 0};
    pfds[1] = {wakeup_pipe_[0],  POLLIN, 0};

    // Buffer sized for a reasonable batch of events
    alignas(struct inotify_event)
        char buf[sizeof(struct inotify_event) + NAME_MAX + 1];

    while (watching_) {
      pfds[0].revents = 0;
      pfds[1].revents = 0;

      int nready = ::poll(pfds.data(), pfds.size(), -1 /*block indefinitely*/);
      if (nready < 0) {
        if (errno == EINTR)
          continue;
        logger_->error("poll() in file watcher failed: {}", std::strerror(errno));
        break;
      }

      // Shutdown signal received via self-pipe
      if (pfds[1].revents & POLLIN)
        break;

      if (!(pfds[0].revents & POLLIN))
        continue;

      // Drain all pending inotify events
      while (true) {
        ssize_t n = ::read(ifd, buf, sizeof(buf));
        if (n < 0) {
          if (errno == EAGAIN || errno == EWOULDBLOCK)
            break; // no more events right now
          logger_->error("read() on inotify fd failed: {}", std::strerror(errno));
          break;
        }

        for (ssize_t offset = 0; offset < n; ) {
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          const auto* ev =
              reinterpret_cast<const struct inotify_event*>(buf + offset);
          offset += static_cast<ssize_t>(sizeof(struct inotify_event) +
                                         ev->len);

          if (ev->len == 0 || ev->name[0] == '\0')
            continue;

          std::string_view name(ev->name);
          bool is_watched = std::any_of(
              kWatched.begin(), kWatched.end(),
              [&](std::string_view w) { return w == name; });
          if (!is_watched)
            continue;

          FileChangeCallback cb_copy;
          {
            std::lock_guard<std::mutex> lock(watcher_mutex_);
            logger_->info("File changed: {}", name);
            cb_copy = change_callback_;
          }
          rebuild_search_index();
          if (cb_copy)
            cb_copy(std::string(name));
        }
      }
    }

    ::inotify_rm_watch(ifd, wd);
    ::close(ifd);
    // Drain and close the read end of the wake pipe
    ::close(wakeup_pipe_[0]);
    wakeup_pipe_[0] = -1;
  });

  logger_->info("File watcher started (inotify) for workspace: {}",
                workspace_path_.string());
}

void MemoryManager::StopFileWatcher() {
  if (!watching_)
    return;
  watching_ = false;
  // Unblock the poll() call in the watcher thread via the self-pipe
  if (wakeup_pipe_[1] >= 0) {
    const char byte = 1;
    ::write(wakeup_pipe_[1], &byte, 1);
    ::close(wakeup_pipe_[1]);
    wakeup_pipe_[1] = -1;
  }
  if (watcher_thread_ && watcher_thread_->joinable()) {
    watcher_thread_->join();
  }
  watcher_thread_.reset();
  logger_->info("File watcher stopped");
}

void MemoryManager::SetFileChangeCallback(FileChangeCallback cb) {
  std::lock_guard<std::mutex> lock(watcher_mutex_);
  change_callback_ = std::move(cb);
}

const std::filesystem::path& MemoryManager::GetWorkspacePath() const {
  return workspace_path_;
}

void MemoryManager::SetAgentWorkspace(const std::string& agent_id) {
  agent_id_ = agent_id;
  workspace_path_ = base_dir_ / "agents" / agent_id / "workspace";
  std::filesystem::create_directories(workspace_path_);
  logger_->info("Set agent workspace: {}", workspace_path_.string());
}

std::filesystem::path MemoryManager::GetBaseDir() const {
  return base_dir_;
}

std::filesystem::path
MemoryManager::GetSessionsDir(const std::string& agent_id) const {
  return base_dir_ / "agents" / agent_id / "sessions";
}

bool MemoryManager::is_memory_file(
    const std::filesystem::path& filepath) const {
  auto filename = filepath.filename().string();

  if (filename == "SOUL.md" || filename == "USER.md" ||
      filename == "MEMORY.md" || filename == "AGENTS.md" ||
      filename == "TOOLS.md") {
    return true;
  }

  if (filepath.parent_path().filename() == "memory" &&
      filepath.extension() == ".md") {
    return true;
  }

  return false;
}

std::string
MemoryManager::read_file_content(const std::filesystem::path& filepath) const {
  if (!std::filesystem::exists(filepath)) {
    throw std::runtime_error("File not found: " + filepath.string());
  }

  std::ifstream file(filepath);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file: " + filepath.string());
  }

  std::ostringstream content;
  content << file.rdbuf();

  return content.str();
}

void MemoryManager::write_file_content(const std::filesystem::path& filepath,
                                       const std::string& content) const {
  std::ofstream file(filepath);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to write file: " + filepath.string());
  }

  file << content;
}

}  // namespace quantclaw
