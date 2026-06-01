// 种子数据自动导入器（config/seed_importer）的单元测试。
// 需要本地 MySQL 运行，auth_socket 用户 ljt 免密登录。
// 测试自动清理导入的数据，确保可重复运行。
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include <cppconn/statement.h>
#include <cppconn/resultset.h>
#include <cppconn/exception.h>

#include "common/log.h"
#include "config/config.h"
#include "config/seeder.h"
#include "config/seed_importer.h"
#include "db/connection_pool.h"
#include "db/dao.h"
#include "model/problem.h"

namespace vibeoj {
namespace {

// ── 测试夹具 — 初始化连接池，管理测试数据清理 ───────────────────

class SeedImporterTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    ServerConfig cfg = load_config();
    ConnectionPool::instance().init(cfg);
    Logger::instance().init(cfg.log_dir);
  }

  static void TearDownTestSuite() {
    ConnectionPool::instance().shutdown();
    Logger::instance().close();
  }

  void SetUp() override {
    created_problem_ids_.clear();
  }

  void TearDown() override {
    // 级联删除测试中创建的题目（test_cases 通过外键自动删除）
    auto guard = ConnectionPool::instance().acquire();
    if (!guard) return;
    for (auto id : created_problem_ids_) {
      ProblemDAO::Delete(*guard, id);
    }
  }

  // 删除数据库中所有题目及其测试用例（为幂等性测试准备干净环境）。
  void clear_all_problems() {
    auto guard = ConnectionPool::instance().acquire();
    if (!guard) return;
    try {
      auto stmt = guard->createStatement();
      stmt->executeUpdate("DELETE FROM test_cases");
      delete stmt;
      stmt = guard->createStatement();
      stmt->executeUpdate("DELETE FROM problems");
      delete stmt;
    } catch (const sql::SQLException& e) {
      LOG_ERROR("clear_all_problems: %s", e.what());
    }
  }

  // 查询当前 problems 表行数，失败返回 -1。
  int64_t count_problems() {
    auto guard = ConnectionPool::instance().acquire();
    if (!guard) return -1;
    try {
      auto stmt = guard->createStatement();
      auto rs = stmt->executeQuery("SELECT COUNT(*) FROM problems");
      int64_t count = -1;
      if (rs->next()) count = rs->getInt64(1);
      delete rs;
      delete stmt;
      return count;
    } catch (const sql::SQLException& e) {
      return -1;
    }
  }

  // 创建包含两道题目的 SeedData 用于测试。
  SeedData make_test_seed() {
    SeedData data;
    {
      Problem p;
      p.title = "Test Problem A";
      p.description = "Description for problem A";
      p.difficulty = Difficulty::easy;
      p.time_limit_ms = 2000;
      p.memory_limit_kb = 131072;
      p.test_cases.push_back(
          TestCase{0, 0, "1 2", "3", true, 0});
      p.test_cases.push_back(
          TestCase{0, 0, "4 5", "9", false, 1});
      data.problems.push_back(std::move(p));
    }
    {
      Problem p;
      p.title = "Test Problem B";
      p.description = "Description for problem B";
      p.difficulty = Difficulty::hard;
      p.time_limit_ms = 5000;
      p.memory_limit_kb = 524288;
      p.test_cases.push_back(
          TestCase{0, 0, "hello", "world", true, 0});
      data.problems.push_back(std::move(p));
    }
    return data;
  }

  // 记录导入后查询到的所有题目 ID，供 TearDown 清理。
  std::vector<int64_t> created_problem_ids_;
};

// ── 测试用例 ──────────────────────────────────────────────────────

TEST_F(SeedImporterTest, ImportIntoEmptyDatabase) {
  clear_all_problems();
  ASSERT_EQ(count_problems(), 0) << "Database should be empty before test";

  auto seed = make_test_seed();
  auto result = import_seed_data(seed);

  EXPECT_EQ(result.problems_imported, 2);
  EXPECT_EQ(result.test_cases_imported, 3);  // 2 + 1

  // 验证数据已写入
  EXPECT_EQ(count_problems(), 2);

  // 查询并记录 ID 用于清理
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);
  auto list = ProblemDAO::FindAll(*guard);
  EXPECT_EQ(list.total, 2);
  for (const auto& p : list.problems) {
    created_problem_ids_.push_back(p.id);
    // 验证测试用例已关联
    auto cases = TestCaseDAO::FindByProblemId(*guard, p.id);
    if (p.title == "Test Problem A") {
      EXPECT_EQ(cases.size(), 2);
    } else if (p.title == "Test Problem B") {
      EXPECT_EQ(cases.size(), 1);
    }
  }
}

TEST_F(SeedImporterTest, IdempotentImport) {
  clear_all_problems();
  ASSERT_EQ(count_problems(), 0) << "Database should be empty before test";

  auto seed = make_test_seed();

  // 第一次导入应成功
  auto result1 = import_seed_data(seed);
  EXPECT_EQ(result1.problems_imported, 2);
  EXPECT_EQ(result1.test_cases_imported, 3);
  EXPECT_EQ(count_problems(), 2);

  // 记录创建的题目 ID（供清理）
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);
  auto list = ProblemDAO::FindAll(*guard);
  for (const auto& p : list.problems) {
    created_problem_ids_.push_back(p.id);
  }

  // 第二次导入应跳过（幂等）
  auto result2 = import_seed_data(seed);
  EXPECT_EQ(result2.problems_imported, 0);
  EXPECT_EQ(result2.test_cases_imported, 0);
  EXPECT_EQ(count_problems(), 2) << "Should still have only 2 problems";
}

TEST_F(SeedImporterTest, ImportEmptySeedData) {
  SeedData empty;
  auto result = import_seed_data(empty);
  EXPECT_EQ(result.problems_imported, 0);
  EXPECT_EQ(result.test_cases_imported, 0);
}

TEST_F(SeedImporterTest, ImportPreservesDifficulty) {
  clear_all_problems();

  auto seed = make_test_seed();
  import_seed_data(seed);

  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);
  auto list = ProblemDAO::FindAll(*guard);
  for (const auto& p : list.problems) {
    created_problem_ids_.push_back(p.id);
    if (p.title == "Test Problem A") {
      EXPECT_EQ(p.difficulty, Difficulty::easy);
      EXPECT_EQ(p.time_limit_ms, 2000);
      EXPECT_EQ(p.memory_limit_kb, 131072);
    } else if (p.title == "Test Problem B") {
      EXPECT_EQ(p.difficulty, Difficulty::hard);
      EXPECT_EQ(p.time_limit_ms, 5000);
      EXPECT_EQ(p.memory_limit_kb, 524288);
    }
  }
}

TEST_F(SeedImporterTest, ImportPreservesSampleFlag) {
  clear_all_problems();

  auto seed = make_test_seed();
  import_seed_data(seed);

  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);
  auto list = ProblemDAO::FindAll(*guard);
  for (const auto& p : list.problems) {
    created_problem_ids_.push_back(p.id);
    auto cases = TestCaseDAO::FindByProblemId(*guard, p.id);
    for (const auto& tc : cases) {
      if (tc.input == "1 2") {
        EXPECT_TRUE(tc.is_sample) << "First case of Problem A should be sample";
      } else if (tc.input == "4 5") {
        EXPECT_FALSE(tc.is_sample) << "Second case of Problem A should be hidden";
      }
    }
  }
}

TEST_F(SeedImporterTest, ImportPreservesOrderIndex) {
  clear_all_problems();

  auto seed = make_test_seed();
  import_seed_data(seed);

  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);
  auto list = ProblemDAO::FindAll(*guard);
  for (const auto& p : list.problems) {
    created_problem_ids_.push_back(p.id);
    if (p.title == "Test Problem A") {
      auto cases = TestCaseDAO::FindByProblemId(*guard, p.id);
      ASSERT_EQ(cases.size(), 2);
      EXPECT_EQ(cases[0].order_index, 0);
      EXPECT_EQ(cases[1].order_index, 1);
    }
  }
}

TEST_F(SeedImporterTest, ImportProblemWithNoTestCases) {
  clear_all_problems();

  SeedData data;
  {
    Problem p;
    p.title = "Problem Without Tests";
    p.description = "This problem has no test cases";
    p.difficulty = Difficulty::medium;
    data.problems.push_back(std::move(p));
  }

  auto result = import_seed_data(data);
  EXPECT_EQ(result.problems_imported, 1);
  EXPECT_EQ(result.test_cases_imported, 0);
  EXPECT_EQ(count_problems(), 1);

  // 清理
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);
  auto list = ProblemDAO::FindAll(*guard);
  for (const auto& p : list.problems) {
    created_problem_ids_.push_back(p.id);
  }
}

TEST_F(SeedImporterTest, ImportOnlyWhenProblemsTableIsEmpty) {
  // 不清理现有数据，验证导入跳过逻辑
  int64_t before = count_problems();
  ASSERT_GE(before, 0);

  auto seed = make_test_seed();
  auto result = import_seed_data(seed);

  if (before == 0) {
    // 数据库为空 → 应导入
    EXPECT_GT(result.problems_imported, 0);
    // 记录用于清理
    auto guard = ConnectionPool::instance().acquire();
    if (guard) {
      auto list = ProblemDAO::FindAll(*guard);
      for (const auto& p : list.problems) {
        created_problem_ids_.push_back(p.id);
      }
    }
  } else {
    // 数据库已有数据 → 应跳过
    EXPECT_EQ(result.problems_imported, 0);
  }
}

TEST_F(SeedImporterTest, ParseRealSeedFileAndImport) {
  clear_all_problems();
  ASSERT_EQ(count_problems(), 0);

  // 解析真实的 seed.yaml
  auto seed = parse_seed_file("../data/seed.yaml");
  ASSERT_EQ(seed.problems.size(), 2);

  auto result = import_seed_data(seed);
  EXPECT_EQ(result.problems_imported, 2);
  EXPECT_EQ(result.test_cases_imported, 6);  // 3 + 3
  EXPECT_EQ(count_problems(), 2);

  // 记录 ID 用于清理
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);
  auto list = ProblemDAO::FindAll(*guard);
  for (const auto& p : list.problems) {
    created_problem_ids_.push_back(p.id);
    // 验证每道题有 3 个测试用例
    auto cases = TestCaseDAO::FindByProblemId(*guard, p.id);
    EXPECT_EQ(cases.size(), 3);
  }
}

TEST_F(SeedImporterTest, CreatedByIsNullForSeedProblems) {
  clear_all_problems();

  auto seed = make_test_seed();
  import_seed_data(seed);

  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);
  auto list = ProblemDAO::FindAll(*guard);
  for (const auto& p : list.problems) {
    created_problem_ids_.push_back(p.id);
    // 种子数据没有创建者，created_by 应为 0（NULL）
    EXPECT_EQ(p.created_by, 0);
  }
}

}  // namespace
}  // namespace vibeoj
