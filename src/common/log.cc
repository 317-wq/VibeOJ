// 日志记录模块实现 — 单例模式 + 线程安全 + 按日期滚动日志文件。

#include "common/log.h"

#include <cstdarg>
#include <cstdio>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>

namespace vibeoj {

Logger& Logger::instance() {
  static Logger logger;
  return logger;
}

Logger::~Logger() {
  close();
}

void Logger::init(const std::string& log_dir) {
  std::lock_guard<std::mutex> lock(mutex_);

  // 尝试创建日志目录（若不存在）
  // 0755: owner rwx, group r-x, others r-x
  mkdir(log_dir.c_str(), 0755);

  // 按日期生成日志文件名
  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);
  auto tm_now = *std::localtime(&time_t_now);

  std::ostringstream oss;
  oss << log_dir << "/vibeoj_"
      << std::setfill('0')
      << std::setw(4) << (tm_now.tm_year + 1900)
      << std::setw(2) << (tm_now.tm_mon + 1)
      << std::setw(2) << tm_now.tm_mday
      << ".log";
  current_log_path_ = oss.str();

  // 先关闭旧文件（如果之前打开过），再打开新文件
  if (file_.is_open()) {
    file_.close();
  }

  file_.open(current_log_path_, std::ios::out | std::ios::app);
  if (!file_.is_open()) {
    std::fprintf(stderr, "[LOG] Warning: Cannot open log file: %s\n",
                 current_log_path_.c_str());
  }
}

void Logger::close() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (file_.is_open()) {
    file_.close();
  }
}

const char* Logger::level_str(LogLevel level) {
  switch (level) {
    case LogLevel::DEBUG:   return "DEBUG";
    case LogLevel::INFO:    return "INFO ";
    case LogLevel::WARNING: return "WARN ";
    case LogLevel::ERROR:   return "ERROR";
    case LogLevel::FATAL:   return "FATAL";
  }
  return "?????";
}

std::string Logger::timestamp() {
  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);
  auto tm_now = *std::localtime(&time_t_now);

  std::ostringstream oss;
  oss << std::setfill('0')
      << std::setw(4) << (tm_now.tm_year + 1900) << '-'
      << std::setw(2) << (tm_now.tm_mon + 1) << '-'
      << std::setw(2) << tm_now.tm_mday << ' '
      << std::setw(2) << tm_now.tm_hour << ':'
      << std::setw(2) << tm_now.tm_min << ':'
      << std::setw(2) << tm_now.tm_sec;
  return oss.str();
}

void Logger::log(LogLevel level, const char* file, int line,
                 const char* fmt, ...) {
  // 格式化用户消息
  char msg_buf[4096];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
  va_end(args);

  // 获取线程 ID 用于多线程调试
  std::ostringstream tid_oss;
  tid_oss << std::this_thread::get_id();
  std::string tid_str = tid_oss.str();

  // 组装完整日志行
  // 格式: [时间戳] [等级] [文件:行] [线程ID] 消息
  // 时间戳格式: YYYY-MM-DD HH:MM:SS
  std::string line_str = "[" + timestamp() + "] [" + level_str(level) + "] [" +
                         file + ":" + std::to_string(line) + "] [" + tid_str +
                         "] " + msg_buf + "\n";

  std::lock_guard<std::mutex> lock(mutex_);

  // 写入文件（若已初始化）
  if (file_.is_open()) {
    file_ << line_str;
    file_.flush();  // 立即刷新，避免崩溃时丢失日志
  }

  // 同步输出到 stderr，方便开发时终端查看
  std::fprintf(stderr, "%s", line_str.c_str());
}

}  // namespace vibeoj
