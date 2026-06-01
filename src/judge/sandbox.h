// 判题沙箱 — fork 子进程，通过 ulimit 限制资源（CPU/内存/进程数/写文件），管道重定向 I/O。
#pragma once

#include <string>

namespace vibeoj {
struct JudgeResult {
  std::string status;   // "accepted", "wrong_answer", "time_limit", etc.
  int time_used_ms;
  int memory_used_kb;
  std::string diff_output;  // set on wrong_answer
  std::string error_output; // set on runtime_error
};

JudgeResult run_in_sandbox(const std::string& binary_path,
                           const std::string& input,
                           const std::string& expected_output,
                           int time_limit_ms,
                           int memory_limit_kb);

}  // namespace vibeoj
