// Problem 与 TestCase 数据结构 — 判题系统的核心领域模型。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vibeoj {

enum class Difficulty {
  easy,
  medium,
  hard
};

inline std::string difficulty_to_string(Difficulty d) {
  switch (d) {
    case Difficulty::easy:   return "easy";
    case Difficulty::medium: return "medium";
    case Difficulty::hard:   return "hard";
  }
  return "easy";
}

inline Difficulty difficulty_from_string(const std::string& s) {
  if (s == "medium") return Difficulty::medium;
  if (s == "hard")   return Difficulty::hard;
  return Difficulty::easy;
}

struct TestCase {
  int64_t id = 0;
  int64_t problem_id = 0;
  std::string input;
  std::string expected_output;
  bool is_sample = false;
  int order_index = 0;
};

struct Problem {
  int64_t id = 0;
  std::string title;
  std::string description;
  Difficulty difficulty = Difficulty::easy;
  int time_limit_ms = 1000;
  int memory_limit_kb = 262144;
  int64_t created_by = 0;
  std::string created_at;
  std::vector<TestCase> test_cases;  // not stored in problems table, used for seed/API
};

}  // namespace vibeoj
