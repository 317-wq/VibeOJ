// 日志记录模块 — 提供分级日志输出到 logs/ 目录，便于开发调试。
// 使用宏 LOG_DEBUG / LOG_INFO / LOG_WARNING / LOG_ERROR / LOG_FATAL 记录日志，
// 自动附加时间戳、文件名、行号、线程 ID 和日志等级。
//
// 使用方式：
//   LOG_INFO("用户 %s 登录成功", username.c_str());
//   LOG_ERROR("数据库连接失败: %s", e.what());
//
// 输出格式：
//   [2026-06-01 12:34:56] [INFO ] [main.cc:42] [thread:12345] 用户 admin 登录成功
//
// 日志文件位于项目根目录的 logs/ 文件夹下，按日期命名：vibeoj_20260601.log

#pragma once

#include <string>
#include <mutex>
#include <fstream>

namespace vibeoj
{

  // 日志等级，按严重程度递增。
  enum class LogLevel
  {
    DEBUG = 0, // 调试信息，仅开发时使用
    INFO,      // 一般运行信息
    WARNING,   // 警告，不影响运行但值得关注
    ERROR,     // 错误，功能异常但程序可继续
    FATAL      // 致命错误，通常需要立即终止
  };

  // Logger 单例 — 线程安全地将格式化日志写入文件。
  // 每次进程启动时会创建新的日志文件（按日期命名），同一天内追加写入。
  class Logger
  {
  public:
    // 获取全局唯一实例。
    static Logger &instance();

    // 初始化日志文件路径，若 logs/ 目录不存在则尝试创建。
    // 未调用 init() 时日志仅输出到 stderr。
    // log_dir: 日志目录路径（如 "logs"）。
    void init(const std::string &log_dir);

    // 写入一条日志。
    // level: 日志等级。
    // file: 源文件名（通常由宏传入 __FILE__）。
    // line: 行号（通常由宏传入 __LINE__）。
    // fmt:  printf 风格的格式化字符串。
    // ...:  可变参数。
    void log(LogLevel level, const char *file, int line, const char *fmt, ...);

    // 关闭日志文件（主要用于测试 teardown，避免文件句柄残留）。
    void close();

  private:
    Logger() = default;
    ~Logger();
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    // 将 LogLevel 转为可读字符串（如 "INFO "）。
    static const char *level_str(LogLevel level);

    // 获取当前时间戳字符串 "YYYY-MM-DD HH:MM:SS"。
    static std::string timestamp();

    std::mutex mutex_;
    std::ofstream file_;
    std::string current_log_path_; // 记录当前日志文件路径，便于测试读取
  };

} // namespace vibeoj

// ── 便捷宏：自动捕获 __FILE__ 和 __LINE__ ────────────────────

#define LOG_DEBUG(fmt, ...) \
  vibeoj::Logger::instance().log(vibeoj::LogLevel::DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) \
  vibeoj::Logger::instance().log(vibeoj::LogLevel::INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_WARNING(fmt, ...) \
  vibeoj::Logger::instance().log(vibeoj::LogLevel::WARNING, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
  vibeoj::Logger::instance().log(vibeoj::LogLevel::ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_FATAL(fmt, ...) \
  vibeoj::Logger::instance().log(vibeoj::LogLevel::FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
