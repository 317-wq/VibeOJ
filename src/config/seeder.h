// YAML 种子数据解析器 — 将 data/seed.yaml 解析为结构化的 Problem 对象。
#pragma once

#include <string>
#include <vector>
#include "model/problem.h"

namespace vibeoj {

struct SeedData {
  std::vector<Problem> problems;
};

// Parse a YAML seed file and return structured data.
// Throws std::runtime_error on parse failure.
SeedData parse_seed_file(const std::string& filepath);

}  // namespace vibeoj
