// Submission 数据结构 — 追踪代码、判题状态、耗时和判题结果。
#pragma once

#include <cstdint>
#include <string>

namespace vibeoj {

enum class SubmissionStatus {
  pending,
  compiling,
  running,
  accepted,
  wrong_answer,
  time_limit,
  memory_limit,
  runtime_error,
  compile_error,
  system_error
};

inline std::string submission_status_to_string(SubmissionStatus s) {
  switch (s) {
    case SubmissionStatus::pending:       return "pending";
    case SubmissionStatus::compiling:     return "compiling";
    case SubmissionStatus::running:       return "running";
    case SubmissionStatus::accepted:      return "accepted";
    case SubmissionStatus::wrong_answer:  return "wrong_answer";
    case SubmissionStatus::time_limit:    return "time_limit";
    case SubmissionStatus::memory_limit:  return "memory_limit";
    case SubmissionStatus::runtime_error: return "runtime_error";
    case SubmissionStatus::compile_error: return "compile_error";
    case SubmissionStatus::system_error:  return "system_error";
  }
  return "pending";
}

inline SubmissionStatus submission_status_from_string(const std::string& s) {
  if (s == "compiling")      return SubmissionStatus::compiling;
  if (s == "running")        return SubmissionStatus::running;
  if (s == "accepted")       return SubmissionStatus::accepted;
  if (s == "wrong_answer")   return SubmissionStatus::wrong_answer;
  if (s == "time_limit")     return SubmissionStatus::time_limit;
  if (s == "memory_limit")   return SubmissionStatus::memory_limit;
  if (s == "runtime_error")  return SubmissionStatus::runtime_error;
  if (s == "compile_error")  return SubmissionStatus::compile_error;
  if (s == "system_error")   return SubmissionStatus::system_error;
  return SubmissionStatus::pending;
}

struct Submission {
  int64_t id;
  int64_t user_id;
  int64_t problem_id;
  std::string code;
  SubmissionStatus status;
  std::string compile_output;
  int passed_cases;
  int total_cases;
  int time_used_ms;
  int memory_used_kb;
  std::string diff_output;
  std::string created_at;
  std::string updated_at;
};

}  // namespace vibeoj
