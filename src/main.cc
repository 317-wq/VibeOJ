// VibeOJ 服务入口 — 加载种子数据，启动 HTTP 服务器（Phase 2 实现）。
#include <cstdlib>
#include <iostream>
#include <string>

#include "config/seeder.h"

int main(int argc, char** argv) {
  // Phase 1: verify seed file parsing works
  const char* seed_file = std::getenv("SEED_FILE");
  if (!seed_file) {
    seed_file = "data/seed.yaml";
  }

  std::cout << "VibeOJ Server starting..." << std::endl;
  std::cout << "Seed file: " << seed_file << std::endl;

  try {
    auto data = vibeoj::parse_seed_file(seed_file);
    std::cout << "Parsed " << data.problems.size() << " problems from seed file." << std::endl;
    for (const auto& p : data.problems) {
      std::cout << "  - [" << vibeoj::difficulty_to_string(p.difficulty) << "] "
                << p.title << " (" << p.test_cases.size() << " test cases)" << std::endl;
    }
  } catch (const std::exception& e) {
    std::cerr << "Failed to parse seed file: " << e.what() << std::endl;
    return 1;
  }

  // Phase 2: start HTTP server, connection pool, judge runner, etc.
  std::cout << "Server running (stub — HTTP server coming in Phase 2)" << std::endl;
  return 0;
}
