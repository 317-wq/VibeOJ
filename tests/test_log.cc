// 日志记录模块（common/log）的单元测试。

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

#include "common/log.h"

namespace vibeoj {
namespace {

// ── 测试辅助函数 ──────────────────────────────────────────────

// 读取文件全部内容为字符串。
static std::string read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in.is_open()) return "";
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

// 递归删除目录及其内容。
static void remove_dir(const std::string& path) {
  std::string cmd = "rm -rf \"" + path + "\"";
  std::system(cmd.c_str());
}

// 在指定目录中查找第一个 .log 文件，返回完整路径；找不到返回空串。
static std::string find_log_file(const std::string& dir) {
  DIR* dp = opendir(dir.c_str());
  if (dp == nullptr) return "";

  std::string result;
  struct dirent* entry;
  while ((entry = readdir(dp)) != nullptr) {
    const char* name = entry->d_name;
    size_t len = std::strlen(name);
    if (len > 4 && std::strcmp(name + len - 4, ".log") == 0) {
      result = dir + "/" + name;
      break;
    }
  }
  closedir(dp);
  return result;
}

// ── 测试夹具 ──────────────────────────────────────────────────

class LogTest : public ::testing::Test {
 protected:
  static constexpr const char* TEST_DIR = "logs_test";

  void SetUp() override {
    // 每次测试前清理：关闭旧日志文件，删除测试目录，重新初始化
    Logger::instance().close();
    remove_dir(TEST_DIR);
    Logger::instance().init(TEST_DIR);
  }

  void TearDown() override {
    // 先关闭日志文件再删除目录，避免文件句柄残留
    Logger::instance().close();
    remove_dir(TEST_DIR);
  }

  // 读取当前测试日志文件的内容。
  std::string read_log() {
    std::string path = find_log_file(TEST_DIR);
    if (path.empty()) return "";
    return read_file(path);
  }
};

// ── 基本功能测试 ──────────────────────────────────────────────

TEST_F(LogTest, InitCreatesLogDirectory) {
  struct stat st;
  EXPECT_EQ(stat(TEST_DIR, &st), 0) << "Log directory should exist";
  EXPECT_TRUE(S_ISDIR(st.st_mode)) << "Should be a directory";
}

TEST_F(LogTest, InitCreatesLogFile) {
  std::string log_path = find_log_file(TEST_DIR);
  EXPECT_FALSE(log_path.empty()) << "Log file should be created in " << TEST_DIR;

  struct stat st;
  EXPECT_EQ(stat(log_path.c_str(), &st), 0) << "Log file should exist";
}

TEST_F(LogTest, LogMessageWrittenToFile) {
  LOG_INFO("Test message: %d + %d = %d", 1, 2, 3);

  std::string content = read_log();
  EXPECT_NE(content.find("Test message: 1 + 2 = 3"), std::string::npos)
      << "Log should contain the message, got: " << content;
  EXPECT_NE(content.find("[INFO ]"), std::string::npos)
      << "Log should contain INFO level tag";
}

TEST_F(LogTest, LogContainsTimestamp) {
  LOG_DEBUG("Timestamp check");

  std::string content = read_log();
  // 检查时间戳格式: [YYYY-MM-DD HH:MM:SS]
  EXPECT_NE(content.find("["), std::string::npos) << "Should have timestamp bracket";
  EXPECT_NE(content.find("-"), std::string::npos) << "Should have date separator";
  EXPECT_NE(content.find(":"), std::string::npos) << "Should have time separator";
}

// ── 日志等级测试 ──────────────────────────────────────────────

TEST_F(LogTest, AllLogLevelsWritten) {
  LOG_DEBUG("debug msg");
  LOG_INFO("info msg");
  LOG_WARNING("warning msg");
  LOG_ERROR("error msg");
  LOG_FATAL("fatal msg");

  std::string content = read_log();
  EXPECT_NE(content.find("[DEBUG]"), std::string::npos);
  EXPECT_NE(content.find("[INFO ]"), std::string::npos);
  EXPECT_NE(content.find("[WARN ]"), std::string::npos);
  EXPECT_NE(content.find("[ERROR]"), std::string::npos);
  EXPECT_NE(content.find("[FATAL]"), std::string::npos);
}

// ── 文件名和行号测试 ──────────────────────────────────────────

TEST_F(LogTest, FileNameAndLineIncluded) {
  int line_of_log = __LINE__ + 1;
  LOG_INFO("file_and_line test");

  std::string content = read_log();
  EXPECT_NE(content.find("test_log.cc"), std::string::npos)
      << "Log should contain source file name";

  std::string expected = "test_log.cc:" + std::to_string(line_of_log);
  EXPECT_NE(content.find(expected), std::string::npos)
      << "Log should contain correct line: " << expected;
}

// ── 线程安全测试 ──────────────────────────────────────────────

TEST_F(LogTest, MultiThreadWrite) {
  const int num_threads = 8;
  const int msgs_per_thread = 50;
  std::vector<std::thread> threads;

  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back([i, msgs_per_thread]() {
      for (int j = 0; j < msgs_per_thread; ++j) {
        LOG_INFO("Thread %d message %d", i, j);
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // 读取日志文件，统计行数
  std::string content = read_log();
  std::istringstream iss(content);
  int line_count = 0;
  std::string line;
  while (std::getline(iss, line)) {
    if (!line.empty()) ++line_count;
  }

  EXPECT_GE(line_count, num_threads * msgs_per_thread)
      << "All messages should be written without loss (got " << line_count
      << " lines, expected at least " << num_threads * msgs_per_thread << ")";
}

// ── 特殊字符测试 ──────────────────────────────────────────────

TEST_F(LogTest, SpecialCharacters) {
  LOG_INFO("Special chars: %s, %% percent", "100%");

  std::string content = read_log();
  EXPECT_NE(content.find("Special chars: 100%, % percent"), std::string::npos)
      << "Got: " << content;
}

// ── 空消息测试 ────────────────────────────────────────────────

TEST_F(LogTest, EmptyMessage) {
  // 不应崩溃
  LOG_INFO("");
  // 能执行到这里即通过
  SUCCEED();
}

// ── LogLevel 枚举值测试 ───────────────────────────────────────

TEST_F(LogTest, LogLevelOrder) {
  EXPECT_LT(static_cast<int>(LogLevel::DEBUG), static_cast<int>(LogLevel::INFO));
  EXPECT_LT(static_cast<int>(LogLevel::INFO), static_cast<int>(LogLevel::WARNING));
  EXPECT_LT(static_cast<int>(LogLevel::WARNING), static_cast<int>(LogLevel::ERROR));
  EXPECT_LT(static_cast<int>(LogLevel::ERROR), static_cast<int>(LogLevel::FATAL));
}

// ── close() 方法测试 ──────────────────────────────────────────

TEST_F(LogTest, CloseAndReopen) {
  LOG_INFO("Before close");
  Logger::instance().close();

  // 重新初始化，应该能继续写入
  Logger::instance().init(TEST_DIR);
  LOG_INFO("After reopen");

  std::string content = read_log();
  EXPECT_NE(content.find("Before close"), std::string::npos);
  EXPECT_NE(content.find("After reopen"), std::string::npos);
}

// ── 长消息测试 ────────────────────────────────────────────────

TEST_F(LogTest, LongMessage) {
  std::string long_str(3000, 'X');
  LOG_ERROR("Long: %s", long_str.c_str());

  std::string content = read_log();
  size_t pos = content.find("Long: ");
  ASSERT_NE(pos, std::string::npos) << "Should contain 'Long:' prefix";
  // 消息缓冲区为 4096 字节，应保留大部分内容
  EXPECT_GE(content.size() - pos, 2000)
      << "Long message should not be truncated too much";
}

}  // namespace
}  // namespace vibeoj
