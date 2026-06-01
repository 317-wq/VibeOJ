// 种子数据自动导入器实现 — 在数据库首次启动时写入预置题目。
#include "config/seed_importer.h"

#include <cppconn/statement.h>
#include <cppconn/resultset.h>
#include <cppconn/exception.h>

#include "common/log.h"
#include "db/connection_pool.h"
#include "db/dao.h"

namespace vibeoj {

ImportResult import_seed_data(SeedData& data) {
  ImportResult result;

  if (data.problems.empty()) {
    LOG_INFO("Seed data is empty, nothing to import");
    return result;
  }

  // 从连接池获取数据库连接
  auto guard = ConnectionPool::instance().acquire();
  if (!guard) {
    LOG_ERROR("Cannot acquire DB connection for seed data import");
    return result;
  }

  // 幂等性检查：若 problems 表已有数据则跳过导入
  try {
    auto stmt = guard->createStatement();
    auto rs = stmt->executeQuery("SELECT COUNT(*) FROM problems");
    if (rs->next()) {
      int64_t count = rs->getInt64(1);
      if (count > 0) {
        LOG_INFO("Database already contains %ld problem(s), skipping seed import",
                 count);
        delete rs;
        delete stmt;
        return result;
      }
    }
    delete rs;
    delete stmt;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("Failed to query existing problems: %s", e.what());
    return result;
  }

  // 逐题导入，每道题及其测试用例分别写入
  for (auto& problem : data.problems) {
    int64_t problem_id = ProblemDAO::Create(*guard, problem);
    if (problem_id <= 0) {
      LOG_ERROR("Failed to insert problem '%s' into database",
                problem.title.c_str());
      continue;
    }

    LOG_DEBUG("Seed problem imported [id=%ld]: %s", problem_id,
              problem.title.c_str());
    result.problems_imported++;

    // 写入该题目的所有测试用例
    for (auto& tc : problem.test_cases) {
      tc.problem_id = problem_id;
      int64_t tc_id = TestCaseDAO::Create(*guard, tc);
      if (tc_id <= 0) {
        LOG_ERROR("Failed to insert test case for problem %ld (order_index=%d)",
                  problem_id, tc.order_index);
        continue;
      }
      result.test_cases_imported++;
    }
  }

  LOG_INFO("Seed data import finished: %d problem(s), %d test case(s)",
           result.problems_imported, result.test_cases_imported);
  return result;
}

}  // namespace vibeoj
