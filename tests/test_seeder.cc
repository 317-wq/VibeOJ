// 种子数据解析器（config/seeder）的单元测试。
#include <gtest/gtest.h>

#include <fstream>
#include <cstdio>
#include <stdexcept>

#include "config/seeder.h"
#include "model/problem.h"

namespace vibeoj {
namespace {

class SeederTest : public ::testing::Test {
 protected:
  // Create a temporary YAML file for testing
  std::string create_temp_yaml(const std::string& content) {
    char temp_path[] = "/tmp/vibeoj_test_XXXXXX";
    int fd = mkstemp(temp_path);
    if (fd == -1) {
      throw std::runtime_error("Failed to create temp file");
    }
    std::ofstream out(temp_path);
    out << content;
    out.close();
    close(fd);
    return std::string(temp_path);
  }
};

TEST_F(SeederTest, ParseSingleProblem) {
  std::string yaml = R"(
problems:
  - title: "Test Problem"
    description: "A test problem description"
    difficulty: easy
    time_limit_ms: 2000
    memory_limit_kb: 131072
    test_cases:
      - input: "1\n2\n"
        expected_output: "3\n"
        is_sample: true
  )";

  auto path = create_temp_yaml(yaml);
  auto data = parse_seed_file(path);

  ASSERT_EQ(data.problems.size(), 1);
  const auto& p = data.problems[0];
  EXPECT_EQ(p.title, "Test Problem");
  EXPECT_EQ(p.description, "A test problem description");
  EXPECT_EQ(p.difficulty, Difficulty::easy);
  EXPECT_EQ(p.time_limit_ms, 2000);
  EXPECT_EQ(p.memory_limit_kb, 131072);
  ASSERT_EQ(p.test_cases.size(), 1);
  EXPECT_EQ(p.test_cases[0].input, "1\n2\n");
  EXPECT_EQ(p.test_cases[0].expected_output, "3\n");
  EXPECT_TRUE(p.test_cases[0].is_sample);

  std::remove(path.c_str());
}

TEST_F(SeederTest, ParseMultipleProblems) {
  std::string yaml = R"(
problems:
  - title: "Problem A"
    description: "Desc A"
    difficulty: easy
    test_cases:
      - input: "in_a"
        expected_output: "out_a"
  - title: "Problem B"
    description: "Desc B"
    difficulty: hard
    time_limit_ms: 3000
    test_cases:
      - input: "in_b1"
        expected_output: "out_b1"
      - input: "in_b2"
        expected_output: "out_b2"
  )";

  auto path = create_temp_yaml(yaml);
  auto data = parse_seed_file(path);

  ASSERT_EQ(data.problems.size(), 2);

  EXPECT_EQ(data.problems[0].title, "Problem A");
  EXPECT_EQ(data.problems[0].difficulty, Difficulty::easy);
  EXPECT_EQ(data.problems[0].test_cases.size(), 1);

  EXPECT_EQ(data.problems[1].title, "Problem B");
  EXPECT_EQ(data.problems[1].difficulty, Difficulty::hard);
  EXPECT_EQ(data.problems[1].time_limit_ms, 3000);
  EXPECT_EQ(data.problems[1].test_cases.size(), 2);

  std::remove(path.c_str());
}

TEST_F(SeederTest, DefaultValuesForOptionalFields) {
  std::string yaml = R"(
problems:
  - title: "Minimal"
    description: "Just the essentials"
    difficulty: medium
    test_cases:
      - input: "x"
        expected_output: "y"
  )";

  auto path = create_temp_yaml(yaml);
  auto data = parse_seed_file(path);

  ASSERT_EQ(data.problems.size(), 1);
  const auto& p = data.problems[0];
  EXPECT_EQ(p.time_limit_ms, 1000);       // default
  EXPECT_EQ(p.memory_limit_kb, 262144);   // default (256MB)
  EXPECT_FALSE(p.test_cases[0].is_sample); // default

  std::remove(path.c_str());
}

TEST_F(SeederTest, SampleFlagTrueAndFalse) {
  std::string yaml = R"(
problems:
  - title: "Samples"
    description: "Testing sample flags"
    difficulty: easy
    test_cases:
      - input: "a"
        expected_output: "b"
        is_sample: true
      - input: "c"
        expected_output: "d"
        is_sample: false
  )";

  auto path = create_temp_yaml(yaml);
  auto data = parse_seed_file(path);

  const auto& cases = data.problems[0].test_cases;
  ASSERT_EQ(cases.size(), 2);
  EXPECT_TRUE(cases[0].is_sample);
  EXPECT_FALSE(cases[1].is_sample);

  std::remove(path.c_str());
}

TEST_F(SeederTest, DifficultyParsing) {
  auto test_difficulty = [](const std::string& name, Difficulty expected) {
    std::string yaml = "problems:\n  - title: T\n    description: D\n    difficulty: " + name + "\n    test_cases: []\n";
    char temp_path[] = "/tmp/vibeoj_test_XXXXXX";
    int fd = mkstemp(temp_path);
    std::ofstream out(temp_path);
    out << yaml;
    out.close();
    close(fd);
    auto data = parse_seed_file(temp_path);
    std::remove(temp_path);
    EXPECT_EQ(data.problems[0].difficulty, expected) << "for difficulty: " << name;
  };

  test_difficulty("easy", Difficulty::easy);
  test_difficulty("medium", Difficulty::medium);
  test_difficulty("hard", Difficulty::hard);
}

TEST_F(SeederTest, MissingProblemsKey) {
  std::string yaml = "other_key: []\n";
  auto path = create_temp_yaml(yaml);

  EXPECT_THROW(parse_seed_file(path), std::runtime_error);
  std::remove(path.c_str());
}

TEST_F(SeederTest, EmptyProblemsList) {
  std::string yaml = "problems: []\n";
  auto path = create_temp_yaml(yaml);
  auto data = parse_seed_file(path);

  EXPECT_EQ(data.problems.size(), 0);
  std::remove(path.c_str());
}

TEST_F(SeederTest, ParseRealSeedFile) {
  // Parse the actual project seed file
  // Path relative to the build/ directory where tests run
  auto data = parse_seed_file("../data/seed.yaml");

  ASSERT_EQ(data.problems.size(), 2);

  // Problem 1: 两数之和
  EXPECT_EQ(data.problems[0].title, "两数之和");
  EXPECT_EQ(data.problems[0].difficulty, Difficulty::easy);
  EXPECT_EQ(data.problems[0].time_limit_ms, 1000);
  EXPECT_EQ(data.problems[0].memory_limit_kb, 262144);
  EXPECT_EQ(data.problems[0].test_cases.size(), 3);

  // Verify first test case is marked as sample
  bool has_sample = false;
  for (const auto& tc : data.problems[0].test_cases) {
    if (tc.is_sample) has_sample = true;
  }
  EXPECT_TRUE(has_sample) << "Should have at least one sample test case";

  // Problem 2: 反转链表
  EXPECT_EQ(data.problems[1].title, "反转链表");
  EXPECT_EQ(data.problems[1].difficulty, Difficulty::easy);
  EXPECT_EQ(data.problems[1].test_cases.size(), 3);
}

TEST_F(SeederTest, TestCaseOrderIndex) {
  std::string yaml = R"(
problems:
  - title: "Ordered"
    description: "Testing order_index"
    difficulty: easy
    test_cases:
      - input: "first"
        expected_output: "1"
        order_index: 10
      - input: "second"
        expected_output: "2"
        order_index: 20
  )";

  auto path = create_temp_yaml(yaml);
  auto data = parse_seed_file(path);

  const auto& cases = data.problems[0].test_cases;
  ASSERT_EQ(cases.size(), 2);
  EXPECT_EQ(cases[0].order_index, 10);
  EXPECT_EQ(cases[1].order_index, 20);

  std::remove(path.c_str());
}

TEST_F(SeederTest, TestCaseAutoOrderIndex) {
  // When order_index is not specified, it should be auto-assigned (0, 1, 2, ...)
  std::string yaml = R"(
problems:
  - title: "AutoOrder"
    description: "Testing auto order_index"
    difficulty: easy
    test_cases:
      - input: "a"
        expected_output: "A"
      - input: "b"
        expected_output: "B"
      - input: "c"
        expected_output: "C"
  )";

  auto path = create_temp_yaml(yaml);
  auto data = parse_seed_file(path);

  const auto& cases = data.problems[0].test_cases;
  ASSERT_EQ(cases.size(), 3);
  EXPECT_EQ(cases[0].order_index, 0);
  EXPECT_EQ(cases[1].order_index, 1);
  EXPECT_EQ(cases[2].order_index, 2);

  std::remove(path.c_str());
}

}  // namespace
}  // namespace vibeoj
