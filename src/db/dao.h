// 数据访问对象 — 对 User、Problem、Submission、RefreshToken 的增删改查操作。
// 所有方法以 sql::Connection& 为第一参数，由调用方通过连接池获取。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "model/user.h"
#include "model/problem.h"
#include "model/submission.h"
#include "model/refresh_token.h"

namespace sql {
class Connection;
}

namespace vibeoj {

// ── UserDAO ─────────────────────────────────────────────────────

class UserDAO {
 public:
  // 创建用户，返回新用户的 id；失败返回 -1。
  static int64_t Create(sql::Connection& conn, const std::string& username,
                         const std::string& password_hash,
                         UserRole role = UserRole::user);

  // 按用户名查找。
  static std::optional<User> FindByUsername(sql::Connection& conn,
                                            const std::string& username);

  // 按 id 查找。
  static std::optional<User> FindById(sql::Connection& conn, int64_t id);

  // 修改角色。
  static bool UpdateRole(sql::Connection& conn, int64_t id, UserRole role);

  // 修改状态（禁用/启用）。
  static bool UpdateStatus(sql::Connection& conn, int64_t id, UserStatus status);

  // 用户列表。
  static std::vector<User> List(sql::Connection& conn);

  // 删除用户（级联删除关联的 submissions 和 refresh_tokens）。
  static bool Delete(sql::Connection& conn, int64_t id);
};

// ── ProblemDAO ──────────────────────────────────────────────────

class ProblemDAO {
 public:
  // 创建题目，填充 problem.id；失败返回 -1。
  static int64_t Create(sql::Connection& conn, Problem& problem);

  // 按 id 查找（不含测试用例）。
  static std::optional<Problem> FindById(sql::Connection& conn, int64_t id);

  // 题目列表，支持难度筛选和分页。返回 {题目列表, 总数}。
  struct ListResult {
    std::vector<Problem> problems;
    int64_t total = 0;
  };
  static ListResult FindAll(sql::Connection& conn,
                            const std::string& difficulty = "",
                            int page = 1, int page_size = 20);

  // 更新题目。
  static bool Update(sql::Connection& conn, const Problem& problem);

  // 删除题目（级联删除关联的测试用例）。
  static bool Delete(sql::Connection& conn, int64_t id);
};

// ── TestCaseDAO ─────────────────────────────────────────────────

class TestCaseDAO {
 public:
  // 创建测试用例，填充 tc.id；失败返回 -1。
  static int64_t Create(sql::Connection& conn, TestCase& tc);

  // 查询某题目的所有测试用例，按 order_index 排序。
  static std::vector<TestCase> FindByProblemId(sql::Connection& conn,
                                               int64_t problem_id);

  // 更新测试用例。
  static bool Update(sql::Connection& conn, const TestCase& tc);

  // 删除单个测试用例。
  static bool Delete(sql::Connection& conn, int64_t id);

  // 删除某题目的所有测试用例，返回删除数量。
  static int DeleteByProblemId(sql::Connection& conn, int64_t problem_id);
};

// ── SubmissionDAO ───────────────────────────────────────────────

class SubmissionDAO {
 public:
  // 分页查询结果。
  struct ListResult {
    std::vector<Submission> submissions;
    int64_t total = 0;
  };

  // 创建提交记录，填充 sub.id；失败返回 -1。
  static int64_t Create(sql::Connection& conn, Submission& sub);

  // 按 id 查询。
  static std::optional<Submission> FindById(sql::Connection& conn, int64_t id);

  // 查询某用户的所有提交。
  static std::vector<Submission> FindByUserId(sql::Connection& conn,
                                              int64_t user_id);

  // 查询某题目的所有提交。
  static std::vector<Submission> FindByProblemId(sql::Connection& conn,
                                                 int64_t problem_id);

  // 按用户和题目查询提交记录。
  static std::vector<Submission> FindByUserAndProblem(sql::Connection& conn,
                                                      int64_t user_id,
                                                      int64_t problem_id);

  // 分页查询提交列表，支持按 user_id / problem_id 可选筛选。
  // user_id <= 0 表示不过滤用户；problem_id <= 0 表示不过滤题目。
  static ListResult List(sql::Connection& conn,
                         int64_t user_id = 0,
                         int64_t problem_id = 0,
                         int page = 1, int page_size = 20);

  // 更新判题状态和结果。
  static bool UpdateStatus(sql::Connection& conn, int64_t id,
                           SubmissionStatus status, int passed_cases = 0,
                           int total_cases = 0, int time_ms = 0,
                           int memory_kb = 0,
                           const std::string& compile_output = "",
                           const std::string& diff_output = "");

  // 删除提交记录。
  static bool Delete(sql::Connection& conn, int64_t id);
};

// ── RefreshTokenDAO ─────────────────────────────────────────────

class RefreshTokenDAO {
 public:
  // 创建 refresh token 记录，返回 id；失败返回 -1。
  static int64_t Create(sql::Connection& conn, int64_t user_id,
                        const std::string& token_hash,
                        const std::string& expires_at);

  // 按哈希值查找。
  static std::optional<RefreshToken> FindByHash(sql::Connection& conn,
                                                const std::string& token_hash);

  // 删除某用户的所有 refresh token（登出/改密时调用）。
  static int DeleteByUserId(sql::Connection& conn, int64_t user_id);

  // 删除过期的 token（定时清理）。
  static int DeleteExpired(sql::Connection& conn);

  // 按哈希值删除单条记录。
  static bool DeleteByHash(sql::Connection& conn,
                           const std::string& token_hash);
};

}  // namespace vibeoj
