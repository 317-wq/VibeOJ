// MySQL 连接池和 DAO 的单元测试 — 覆盖连接池初始化、获取/归还连接，
// 以及 User/Problem/TestCase/Submission/RefreshToken 的增删改查。
//
// 测试需要本地 MySQL 运行，auth_socket 用户 ljt 免密登录。
// 测试自动清理创建的数据（通过记录插入 ID 并级联删除）。

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <cppconn/statement.h>
#include <cppconn/resultset.h>

#include "common/log.h"
#include "config/config.h"
#include "db/connection_pool.h"
#include "db/dao.h"
#include "model/user.h"
#include "model/problem.h"
#include "model/submission.h"
#include "model/refresh_token.h"

namespace vibeoj {
namespace {

// ── 测试夹具 — 管理连接池初始化和测试数据清理 ───────────────────

class DbTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    // 连接池只需初始化一次
    ServerConfig cfg = load_config();
    ConnectionPool::instance().init(cfg);
    // 初始化日志（否则连接池日志会触发 stderr 输出）
    Logger::instance().init(cfg.log_dir);
  }

  static void TearDownTestSuite() {
    ConnectionPool::instance().shutdown();
    Logger::instance().close();
  }

  void SetUp() override {
    created_users_.clear();
    created_problems_.clear();
    created_submissions_.clear();
    created_tokens_.clear();
  }

  void TearDown() override {
    auto guard = ConnectionPool::instance().acquire();
    if (!guard) return;

    // 级联清理：先删 submission，再删 test_case（外键约束）
    for (auto id : created_submissions_) {
      SubmissionDAO::Delete(*guard, id);
    }
    for (auto id : created_tokens_) {
      RefreshTokenDAO::DeleteByUserId(*guard, id);
    }
    // test_cases 通过删除 problem 级联清理
    for (auto id : created_problems_) {
      ProblemDAO::Delete(*guard, id);
    }
    for (auto id : created_users_) {
      UserDAO::Delete(*guard, id);
    }
  }

  // 辅助：创建一个测试用户
  int64_t create_test_user(const std::string& username = "test_user",
                            const std::string& password_hash = "$2b$12$test",
                            UserRole role = UserRole::user) {
    auto guard = ConnectionPool::instance().acquire();
    if (!guard) return -1;
    int64_t id = UserDAO::Create(*guard, username, password_hash, role);
    if (id > 0) created_users_.push_back(id);
    return id;
  }

  // 辅助：创建一个测试题目
  int64_t create_test_problem(const std::string& title = "Test Problem",
                               int64_t created_by = 0) {
    auto guard = ConnectionPool::instance().acquire();
    if (!guard) return -1;
    Problem p;
    p.title = title;
    p.description = "Test description";
    p.difficulty = Difficulty::easy;
    p.time_limit_ms = 1000;
    p.memory_limit_kb = 262144;
    p.created_by = created_by;
    int64_t id = ProblemDAO::Create(*guard, p);
    if (id > 0) created_problems_.push_back(id);
    return id;
  }

  std::vector<int64_t> created_users_;
  std::vector<int64_t> created_problems_;
  std::vector<int64_t> created_submissions_;
  std::vector<int64_t> created_tokens_;
};

// ── ConnectionPool 测试 ─────────────────────────────────────────

TEST_F(DbTest, PoolInitialized) {
  // SetUpTestSuite 已初始化，此处验证可获取连接
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard) << "Should acquire a connection";
  EXPECT_NE(guard.get(), nullptr);
}

TEST_F(DbTest, PoolAcquireMultipleConnections) {
  // 从池中获取多个连接，验证池的复用能力
  std::vector<ConnectionGuard> guards;
  for (int i = 0; i < 4; i++) {
    auto guard = ConnectionPool::instance().acquire();
    ASSERT_TRUE(guard) << "Failed to acquire connection " << i;
    guards.push_back(std::move(guard));
  }
  EXPECT_EQ(guards.size(), 4);
}

TEST_F(DbTest, PoolAcquireReturnsValidConnection) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  // 执行一个简单查询验证连接可用
  auto stmt = guard->createStatement();
  auto rs = stmt->executeQuery("SELECT 1 AS val");
  ASSERT_TRUE(rs->next());
  EXPECT_EQ(rs->getInt("val"), 1);
  delete rs;
  delete stmt;
}

// ── UserDAO 测试 ────────────────────────────────────────────────

TEST_F(DbTest, UserCreate) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t id = UserDAO::Create(*guard, "test_alice", "hash_alice");
  ASSERT_GT(id, 0);
  created_users_.push_back(id);

  auto user = UserDAO::FindById(*guard, id);
  ASSERT_TRUE(user.has_value());
  EXPECT_EQ(user->username, "test_alice");
  EXPECT_EQ(user->password_hash, "hash_alice");
  EXPECT_EQ(user->role, UserRole::user);
  EXPECT_EQ(user->status, UserStatus::active);
}

TEST_F(DbTest, UserCreateWithAdminRole) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t id = UserDAO::Create(*guard, "test_admin", "admin_hash",
                                UserRole::admin);
  ASSERT_GT(id, 0);
  created_users_.push_back(id);

  auto user = UserDAO::FindById(*guard, id);
  ASSERT_TRUE(user.has_value());
  EXPECT_EQ(user->role, UserRole::admin);
}

TEST_F(DbTest, UserFindByUsernameNotFound) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  auto user = UserDAO::FindByUsername(*guard, "nonexistent_user_42");
  EXPECT_FALSE(user.has_value());
}

TEST_F(DbTest, UserUpdateRole) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t id = UserDAO::Create(*guard, "test_role", "hash");
  ASSERT_GT(id, 0);
  created_users_.push_back(id);

  EXPECT_TRUE(UserDAO::UpdateRole(*guard, id, UserRole::admin));

  auto user = UserDAO::FindById(*guard, id);
  ASSERT_TRUE(user.has_value());
  EXPECT_EQ(user->role, UserRole::admin);
}

TEST_F(DbTest, UserUpdateStatus) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t id = UserDAO::Create(*guard, "test_status", "hash");
  ASSERT_GT(id, 0);
  created_users_.push_back(id);

  EXPECT_TRUE(UserDAO::UpdateStatus(*guard, id, UserStatus::disabled));

  auto user = UserDAO::FindById(*guard, id);
  ASSERT_TRUE(user.has_value());
  EXPECT_EQ(user->status, UserStatus::disabled);
}

TEST_F(DbTest, UserList) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t id1 = UserDAO::Create(*guard, "list_user_a", "hash_a");
  int64_t id2 = UserDAO::Create(*guard, "list_user_b", "hash_b");
  ASSERT_GT(id1, 0); ASSERT_GT(id2, 0);
  created_users_.push_back(id1);
  created_users_.push_back(id2);

  auto users = UserDAO::List(*guard);
  EXPECT_GE(users.size(), 2);
}

TEST_F(DbTest, UserCreateDuplicateUsername) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t id1 = UserDAO::Create(*guard, "dup_user", "hash1");
  ASSERT_GT(id1, 0);
  created_users_.push_back(id1);

  // 重复用户名应返回 -1（MySQL UNIQUE 约束冲突）
  int64_t id2 = UserDAO::Create(*guard, "dup_user", "hash2");
  EXPECT_EQ(id2, -1);
}

// ── ProblemDAO 测试 ─────────────────────────────────────────────

TEST_F(DbTest, ProblemCreate) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  Problem p;
  p.title = "P1";
  p.description = "Description P1";
  p.difficulty = Difficulty::medium;
  p.time_limit_ms = 2000;
  p.memory_limit_kb = 131072;
  p.created_by = 0;

  int64_t id = ProblemDAO::Create(*guard, p);
  ASSERT_GT(id, 0);
  created_problems_.push_back(id);
  EXPECT_EQ(p.id, id);  // 应填充回 problem 结构

  auto found = ProblemDAO::FindById(*guard, id);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->title, "P1");
  EXPECT_EQ(found->difficulty, Difficulty::medium);
  EXPECT_EQ(found->time_limit_ms, 2000);
  EXPECT_EQ(found->memory_limit_kb, 131072);
}

TEST_F(DbTest, ProblemFindByIdNotFound) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  auto p = ProblemDAO::FindById(*guard, 999999);
  EXPECT_FALSE(p.has_value());
}

TEST_F(DbTest, ProblemFindAll) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t id1 = create_test_problem("Problem Alpha");
  int64_t id2 = create_test_problem("Problem Beta");
  ASSERT_GT(id1, 0); ASSERT_GT(id2, 0);

  auto result = ProblemDAO::FindAll(*guard);
  EXPECT_GE(result.total, 2);
  EXPECT_GE(result.problems.size(), 2);
}

TEST_F(DbTest, ProblemFindAllWithDifficultyFilter) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  Problem p;
  p.title = "Hard Problem";
  p.description = "Hard";
  p.difficulty = Difficulty::hard;
  p.time_limit_ms = 3000;
  p.memory_limit_kb = 262144;
  p.created_by = 0;
  int64_t id = ProblemDAO::Create(*guard, p);
  ASSERT_GT(id, 0);
  created_problems_.push_back(id);

  auto result = ProblemDAO::FindAll(*guard, "hard");
  EXPECT_GE(result.total, 1);
  for (const auto& prob : result.problems) {
    EXPECT_EQ(prob.difficulty, Difficulty::hard);
  }
}

TEST_F(DbTest, ProblemFindAllPagination) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  // 确保至少有 1 道题
  int64_t id = create_test_problem("Pagination Test");
  ASSERT_GT(id, 0);

  auto result = ProblemDAO::FindAll(*guard, "", 1, 1);
  EXPECT_EQ(result.problems.size(), 1);
  EXPECT_GE(result.total, 1);
}

TEST_F(DbTest, ProblemUpdate) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t id = create_test_problem("Original");
  ASSERT_GT(id, 0);

  Problem p;
  p.id = id;
  p.title = "Updated Title";
  p.description = "Updated Description";
  p.difficulty = Difficulty::hard;
  p.time_limit_ms = 5000;
  p.memory_limit_kb = 524288;
  EXPECT_TRUE(ProblemDAO::Update(*guard, p));

  auto found = ProblemDAO::FindById(*guard, id);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->title, "Updated Title");
  EXPECT_EQ(found->difficulty, Difficulty::hard);
  EXPECT_EQ(found->time_limit_ms, 5000);
}

TEST_F(DbTest, ProblemDelete) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t id = create_test_problem("To Be Deleted");
  ASSERT_GT(id, 0);

  EXPECT_TRUE(ProblemDAO::Delete(*guard, id));
  // 从清理列表中移除（已手动删除）
  created_problems_.erase(
      std::remove(created_problems_.begin(),
                  created_problems_.end(), id),
      created_problems_.end());

  auto found = ProblemDAO::FindById(*guard, id);
  EXPECT_FALSE(found.has_value());
}

// ── TestCaseDAO 测试 ────────────────────────────────────────────

TEST_F(DbTest, TestCaseCreateAndFind) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t problem_id = create_test_problem("Problem With Test Cases");
  ASSERT_GT(problem_id, 0);

  TestCase tc;
  tc.problem_id = problem_id;
  tc.input = "1 2 3";
  tc.expected_output = "6";
  tc.is_sample = true;
  tc.order_index = 0;
  int64_t id = TestCaseDAO::Create(*guard, tc);
  ASSERT_GT(id, 0);
  EXPECT_EQ(tc.id, id);

  auto cases = TestCaseDAO::FindByProblemId(*guard, problem_id);
  ASSERT_EQ(cases.size(), 1);
  EXPECT_EQ(cases[0].input, "1 2 3");
  EXPECT_EQ(cases[0].expected_output, "6");
  EXPECT_TRUE(cases[0].is_sample);
  EXPECT_EQ(cases[0].order_index, 0);
}

TEST_F(DbTest, TestCaseMultiplePerProblem) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t problem_id = create_test_problem("Multi Case Problem");
  ASSERT_GT(problem_id, 0);

  for (int i = 0; i < 3; i++) {
    TestCase tc;
    tc.problem_id = problem_id;
    tc.input = "input_" + std::to_string(i);
    tc.expected_output = "output_" + std::to_string(i);
    tc.is_sample = (i == 0);
    tc.order_index = i;
    int64_t id = TestCaseDAO::Create(*guard, tc);
    ASSERT_GT(id, 0) << "Failed to create test case " << i;
  }

  auto cases = TestCaseDAO::FindByProblemId(*guard, problem_id);
  ASSERT_EQ(cases.size(), 3);
  // 验证按 order_index 排序
  EXPECT_EQ(cases[0].order_index, 0);
  EXPECT_EQ(cases[1].order_index, 1);
  EXPECT_EQ(cases[2].order_index, 2);
}

TEST_F(DbTest, TestCaseUpdate) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t problem_id = create_test_problem("Update Case Problem");
  ASSERT_GT(problem_id, 0);

  TestCase tc;
  tc.problem_id = problem_id;
  tc.input = "old";
  tc.expected_output = "old_out";
  tc.is_sample = false;
  tc.order_index = 0;
  int64_t id = TestCaseDAO::Create(*guard, tc);
  ASSERT_GT(id, 0);

  tc.id = id;
  tc.input = "new";
  tc.expected_output = "new_out";
  tc.is_sample = true;
  EXPECT_TRUE(TestCaseDAO::Update(*guard, tc));

  auto cases = TestCaseDAO::FindByProblemId(*guard, problem_id);
  ASSERT_EQ(cases.size(), 1);
  EXPECT_EQ(cases[0].input, "new");
  EXPECT_EQ(cases[0].expected_output, "new_out");
  EXPECT_TRUE(cases[0].is_sample);
}

TEST_F(DbTest, TestCaseDelete) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t problem_id = create_test_problem("Delete Case Problem");
  ASSERT_GT(problem_id, 0);

  TestCase tc;
  tc.problem_id = problem_id;
  tc.input = "x";
  tc.expected_output = "y";
  tc.order_index = 0;
  int64_t id = TestCaseDAO::Create(*guard, tc);
  ASSERT_GT(id, 0);

  EXPECT_TRUE(TestCaseDAO::Delete(*guard, id));

  auto cases = TestCaseDAO::FindByProblemId(*guard, problem_id);
  EXPECT_EQ(cases.size(), 0);
}

// ── SubmissionDAO 测试 ──────────────────────────────────────────

TEST_F(DbTest, SubmissionCreate) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t user_id = create_test_user("submission_user");
  ASSERT_GT(user_id, 0);
  int64_t problem_id = create_test_problem("Submission Problem", user_id);
  ASSERT_GT(problem_id, 0);

  Submission sub;
  sub.user_id = user_id;
  sub.problem_id = problem_id;
  sub.code = "#include <iostream>\nint main() { return 0; }";
  int64_t id = SubmissionDAO::Create(*guard, sub);
  ASSERT_GT(id, 0);
  created_submissions_.push_back(id);
  EXPECT_EQ(sub.id, id);

  auto found = SubmissionDAO::FindById(*guard, id);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->user_id, user_id);
  EXPECT_EQ(found->problem_id, problem_id);
  EXPECT_EQ(found->code, sub.code);
  EXPECT_EQ(found->status, SubmissionStatus::pending);  // 默认状态
}

TEST_F(DbTest, SubmissionUpdateStatus) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t user_id = create_test_user("status_user");
  ASSERT_GT(user_id, 0);
  int64_t problem_id = create_test_problem("Status Problem", user_id);
  ASSERT_GT(problem_id, 0);

  Submission sub;
  sub.user_id = user_id;
  sub.problem_id = problem_id;
  sub.code = "int main(){}";
  int64_t id = SubmissionDAO::Create(*guard, sub);
  ASSERT_GT(id, 0);
  created_submissions_.push_back(id);

  EXPECT_TRUE(SubmissionDAO::UpdateStatus(
      *guard, id, SubmissionStatus::accepted, 5, 5, 150, 32000,
      "", ""));

  auto found = SubmissionDAO::FindById(*guard, id);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->status, SubmissionStatus::accepted);
  EXPECT_EQ(found->passed_cases, 5);
  EXPECT_EQ(found->total_cases, 5);
  EXPECT_EQ(found->time_used_ms, 150);
  EXPECT_EQ(found->memory_used_kb, 32000);
}

TEST_F(DbTest, SubmissionFindByUserId) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t user_id = create_test_user("findby_user");
  ASSERT_GT(user_id, 0);
  int64_t problem_id = create_test_problem("FindBy Problem", user_id);
  ASSERT_GT(problem_id, 0);

  for (int i = 0; i < 2; i++) {
    Submission sub;
    sub.user_id = user_id;
    sub.problem_id = problem_id;
    sub.code = "code_" + std::to_string(i);
    int64_t id = SubmissionDAO::Create(*guard, sub);
    ASSERT_GT(id, 0);
    created_submissions_.push_back(id);
  }

  auto subs = SubmissionDAO::FindByUserId(*guard, user_id);
  EXPECT_GE(subs.size(), 2);
}

TEST_F(DbTest, SubmissionFindByProblemId) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t user_id = create_test_user("pb_user");
  ASSERT_GT(user_id, 0);
  int64_t problem_id = create_test_problem("FindByProblemId", user_id);
  ASSERT_GT(problem_id, 0);

  Submission sub;
  sub.user_id = user_id;
  sub.problem_id = problem_id;
  sub.code = "code";
  int64_t id = SubmissionDAO::Create(*guard, sub);
  ASSERT_GT(id, 0);
  created_submissions_.push_back(id);

  auto subs = SubmissionDAO::FindByProblemId(*guard, problem_id);
  EXPECT_GE(subs.size(), 1);
}

TEST_F(DbTest, SubmissionFindByUserAndProblem) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t user_id = create_test_user("uap_user");
  ASSERT_GT(user_id, 0);
  int64_t problem_id = create_test_problem("UAP Problem", user_id);
  ASSERT_GT(problem_id, 0);

  Submission sub;
  sub.user_id = user_id;
  sub.problem_id = problem_id;
  sub.code = "uap code";
  int64_t id = SubmissionDAO::Create(*guard, sub);
  ASSERT_GT(id, 0);
  created_submissions_.push_back(id);

  auto subs = SubmissionDAO::FindByUserAndProblem(*guard, user_id, problem_id);
  EXPECT_GE(subs.size(), 1);
  for (const auto& s : subs) {
    EXPECT_EQ(s.user_id, user_id);
    EXPECT_EQ(s.problem_id, problem_id);
  }
}

// ── RefreshTokenDAO 测试 ────────────────────────────────────────

TEST_F(DbTest, RefreshTokenCreateAndFind) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t user_id = create_test_user("token_user");
  ASSERT_GT(user_id, 0);
  created_tokens_.push_back(user_id);  // TearDown 会按 user_id 清理

  int64_t id = RefreshTokenDAO::Create(*guard, user_id,
                                        "abc123hash",
                                        "2099-12-31 23:59:59");
  ASSERT_GT(id, 0);

  auto token = RefreshTokenDAO::FindByHash(*guard, "abc123hash");
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(token->user_id, user_id);
  EXPECT_EQ(token->token_hash, "abc123hash");
}

TEST_F(DbTest, RefreshTokenFindNotFound) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  auto token = RefreshTokenDAO::FindByHash(*guard, "nonexistent_hash");
  EXPECT_FALSE(token.has_value());
}

TEST_F(DbTest, RefreshTokenDeleteByHash) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t user_id = create_test_user("del_hash_user");
  ASSERT_GT(user_id, 0);
  created_tokens_.push_back(user_id);

  int64_t id = RefreshTokenDAO::Create(*guard, user_id,
                                        "todelete_hash",
                                        "2099-12-31 23:59:59");
  ASSERT_GT(id, 0);

  EXPECT_TRUE(RefreshTokenDAO::DeleteByHash(*guard, "todelete_hash"));
  EXPECT_FALSE(RefreshTokenDAO::FindByHash(*guard, "todelete_hash").has_value());
}

TEST_F(DbTest, RefreshTokenDeleteByUserId) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t user_id = create_test_user("del_user_id_user");
  ASSERT_GT(user_id, 0);
  // 不从 created_tokens_ 清理，手动测试 DeleteByUserId

  RefreshTokenDAO::Create(*guard, user_id, "hash_a", "2099-12-31 23:59:59");
  RefreshTokenDAO::Create(*guard, user_id, "hash_b", "2099-12-31 23:59:59");

  int deleted = RefreshTokenDAO::DeleteByUserId(*guard, user_id);
  EXPECT_EQ(deleted, 2);

  EXPECT_FALSE(RefreshTokenDAO::FindByHash(*guard, "hash_a").has_value());
  EXPECT_FALSE(RefreshTokenDAO::FindByHash(*guard, "hash_b").has_value());
}

TEST_F(DbTest, RefreshTokenDeleteExpired) {
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);

  int64_t user_id = create_test_user("expired_user");
  ASSERT_GT(user_id, 0);
  created_tokens_.push_back(user_id);

  // 创建已过期的 token
  RefreshTokenDAO::Create(*guard, user_id, "expired_hash",
                           "2020-01-01 00:00:00");
  int deleted = RefreshTokenDAO::DeleteExpired(*guard);
  EXPECT_GE(deleted, 0);

  EXPECT_FALSE(
      RefreshTokenDAO::FindByHash(*guard, "expired_hash").has_value());
}

}  // namespace
}  // namespace vibeoj
