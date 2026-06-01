// YAML 种子数据解析器实现，基于 yaml-cpp 库。
#include "config/seeder.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include "yaml-cpp/yaml.h"

namespace vibeoj {

SeedData parse_seed_file(const std::string& filepath) {
  SeedData data;

  YAML::Node root = YAML::LoadFile(filepath);
  if (!root["problems"] || !root["problems"].IsSequence()) {
    throw std::runtime_error("seed file missing 'problems' sequence");
  }

  for (const auto& p : root["problems"]) {
    Problem problem;
    problem.title = p["title"].as<std::string>();
    problem.description = p["description"].as<std::string>();
    problem.difficulty = difficulty_from_string(p["difficulty"].as<std::string>());
    problem.time_limit_ms = p["time_limit_ms"] ? p["time_limit_ms"].as<int>() : 1000;
    problem.memory_limit_kb = p["memory_limit_kb"] ? p["memory_limit_kb"].as<int>() : 262144;

    if (p["test_cases"] && p["test_cases"].IsSequence()) {
      int idx = 0;
      for (const auto& tc : p["test_cases"]) {
        TestCase test_case;
        test_case.input = tc["input"].as<std::string>();
        test_case.expected_output = tc["expected_output"].as<std::string>();
        test_case.is_sample = tc["is_sample"] ? tc["is_sample"].as<bool>() : false;
        test_case.order_index = tc["order_index"] ? tc["order_index"].as<int>() : idx;
        problem.test_cases.push_back(std::move(test_case));
        ++idx;
      }
    }

    data.problems.push_back(std::move(problem));
  }

  return data;
}

}  // namespace vibeoj
