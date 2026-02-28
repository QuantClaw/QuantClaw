#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include "quantclaw/core/memory_manager.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>

class MemoryManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "quantclaw_memory_test";
        std::filesystem::create_directories(test_dir_);

        auto null_sink = std::make_shared<spdlog::sinks::null_sink_mt>();
        logger_ = std::make_shared<spdlog::logger>("test", null_sink);

        memory_manager_ = std::make_unique<quantclaw::MemoryManager>(test_dir_, logger_);
    }

    void TearDown() override {
        if (std::filesystem::exists(test_dir_)) {
            std::filesystem::remove_all(test_dir_);
        }
    }

    std::filesystem::path test_dir_;
    std::shared_ptr<spdlog::logger> logger_;
    std::unique_ptr<quantclaw::MemoryManager> memory_manager_;
};

TEST_F(MemoryManagerTest, ReadIdentityFile) {
    std::ofstream f(test_dir_ / "SOUL.md");
    f << "# My Soul\n\nI am a helpful assistant.";
    f.close();

    auto content = memory_manager_->read_identity_file("SOUL.md");
    EXPECT_EQ(content, "# My Soul\n\nI am a helpful assistant.");
}

TEST_F(MemoryManagerTest, ReadNonExistentFile) {
    EXPECT_THROW(memory_manager_->read_identity_file("NONEXISTENT.md"), std::runtime_error);
}

TEST_F(MemoryManagerTest, ReadAgentsFile) {
    std::ofstream f(test_dir_ / "AGENTS.md");
    f << "# Agent Behavior\nBe concise.";
    f.close();

    auto content = memory_manager_->read_agents_file();
    EXPECT_EQ(content, "# Agent Behavior\nBe concise.");
}

TEST_F(MemoryManagerTest, ReadToolsFile) {
    std::ofstream f(test_dir_ / "TOOLS.md");
    f << "# Tool Guide\nUse read for files.";
    f.close();

    auto content = memory_manager_->read_tools_file();
    EXPECT_EQ(content, "# Tool Guide\nUse read for files.");
}

TEST_F(MemoryManagerTest, SaveDailyMemory) {
    memory_manager_->save_daily_memory("This is a test memory entry.");

    auto memory_dir = test_dir_ / "memory";
    EXPECT_TRUE(std::filesystem::exists(memory_dir));

    bool found = false;
    for (const auto& entry : std::filesystem::directory_iterator(memory_dir)) {
        if (entry.path().extension() == ".md") {
            std::ifstream file(entry.path());
            std::string content((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
            if (content.find("This is a test memory entry.") != std::string::npos) {
                found = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(MemoryManagerTest, SearchMemory) {
    std::ofstream soul(test_dir_ / "SOUL.md");
    soul << "I love quantum physics and lobsters.";
    soul.close();

    std::ofstream user(test_dir_ / "USER.md");
    user << "The user is interested in AI and C++ programming.";
    user.close();

    auto results = memory_manager_->search_memory("quantum");
    EXPECT_FALSE(results.empty());
    EXPECT_TRUE(results[0].find("quantum") != std::string::npos);

    auto no_results = memory_manager_->search_memory("zzz_nonexistent_zzz");
    EXPECT_TRUE(no_results.empty());
}

TEST_F(MemoryManagerTest, GetWorkspacePath) {
    EXPECT_EQ(memory_manager_->get_workspace_path(), test_dir_);
}

TEST_F(MemoryManagerTest, GetSessionsDir) {
    auto sessions_dir = memory_manager_->get_sessions_dir("main");
    EXPECT_TRUE(sessions_dir.string().find("agents/main/sessions") != std::string::npos);
}

TEST_F(MemoryManagerTest, LoadWorkspaceFiles) {
    std::ofstream f(test_dir_ / "SOUL.md");
    f << "test soul";
    f.close();

    // Should not throw
    EXPECT_NO_THROW(memory_manager_->load_workspace_files());
}

// --- File watcher tests ---

TEST_F(MemoryManagerTest, FileWatcherStartStop) {
    // Should not throw
    EXPECT_NO_THROW(memory_manager_->start_file_watcher());
    EXPECT_NO_THROW(memory_manager_->stop_file_watcher());
}

TEST_F(MemoryManagerTest, FileWatcherDetectsChange) {
    // Create initial file
    {
        std::ofstream f(test_dir_ / "SOUL.md");
        f << "initial content";
    }

    bool changed = false;
    std::string changed_file;
    memory_manager_->set_file_change_callback([&](const std::string& filename) {
        changed = true;
        changed_file = filename;
    });

    memory_manager_->start_file_watcher();

    // Wait for initial scan
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Modify the file
    {
        std::ofstream f(test_dir_ / "SOUL.md");
        f << "modified content";
    }

    // Wait for watcher to detect (polls every 5 seconds)
    std::this_thread::sleep_for(std::chrono::seconds(7));

    memory_manager_->stop_file_watcher();

    EXPECT_TRUE(changed);
    EXPECT_EQ(changed_file, "SOUL.md");
}

TEST_F(MemoryManagerTest, FileWatcherDoubleStartIgnored) {
    memory_manager_->start_file_watcher();
    // Second start should be a no-op
    EXPECT_NO_THROW(memory_manager_->start_file_watcher());
    memory_manager_->stop_file_watcher();
}
