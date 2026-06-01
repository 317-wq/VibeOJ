// 代码提交 API 单元测试 — 测试提交创建、查询、列表的 DAO 层操作。
// 对应端点：POST /api/v1/submissions, GET /api/v1/submissions/:id,
//          GET /api/v1/submissions。
//
// 需要本地 MySQL 运行（oj_system 数据库）。所有测试数据在完成后自动清理。

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "common/log.h"
#include "config/config.h"
#include "db/connection_pool.h"
#include "db/dao.h"
#include "model/problem.h"
#include "model/submission.h"
#include "model/user.h"

namespace vibeoj {
namespace {

// ── RAII 环境变量守卫 ──────────────────────────────────────────
class EnvGuard {
 public:
  EnvGuard(const char* name, const char* value)
      : name_(name), old_value_(std::getenv(name)) {
    setenv(name, value, 1);
  }
  ~EnvGuard() {
    if (old_value_) {
      setenv(name_, old_value_, 1);
    } else {
      unsetenv(name_);
    }
  }
 private:
  const char* name_;
  const char* old_value_;
};

// ── 测试夹具 ────────────────────────────────────────────────────
class SubmissionAPITest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 设置默认环境变量
    if (!std::getenv("VIBEOJ_DB_HOST")) {
      env_db_host_ = std::make_unique<EnvGuard>("VIBEOJ_DB_HOST", "localhost");
    }
    if (!std::getenv("VIBEOJ_DB_NAME")) {
      env_db_name_ = std::make_unique<EnvGuard>("VIBEOJ_DB_NAME", "oj_system");
    }
    Logger::instance().init("/tmp/vibeoj_test_logs");

    cfg_ = load_config();
    ConnectionPool::instance().init(cfg_);

    conn_guard_ = ConnectionPool::instance().acquire();

    // 创建测试用户
    test_user_id_ = UserDAO::Create(*conn_guard_, "_test_submit_user",
                                     "$2b$12$dummyhashdummyhashdummyhashdummy");
    if (test_user_id_ <= 0) {
      auto existing = UserDAO::FindByUsername(*conn_guard_, "_test_submit_user");
      if (existing.has_value()) test_user_id_ = existing->id;
    }

    // 创建第二个测试用户（用于验证访问控制）
    test_user2_id_ = UserDAO::Create(*conn_guard_, "_test_submit_user2",
                                      "$2b$12$dummyhashdummyhashdummyhashdummy");
    if (test_user2_id_ <= 0) {
      auto existing = UserDAO::FindByUsername(*conn_guard_, "_test_submit_user2");
      if (existing.has_value()) test_user2_id_ = existing->id;
    }

    // 创建测试题目
    Problem p;
    p.title = "Submit Test Problem";
    p.description = "Problem for submission testing";
    p.difficulty = Difficulty::easy;
    p.time_limit_ms = 1000;
    p.memory_limit_kb = 262144;
    p.created_by = test_user_id_;
    test_problem_id_ = ProblemDAO::Create(*conn_guard_, p);
    ASSERT_GT(test_problem_id_, 0);
  }

  void TearDown() override {
    if (conn_guard_) {
      // 逆序清理：先删提交，再删题目，最后删用户
      for (int64_t sid : created_submission_ids_) {
        SubmissionDAO::Delete(*conn_guard_, sid);
      }
      if (test_problem_id_ > 0) {
        ProblemDAO::Delete(*conn_guard_, test_problem_id_);
      }
      if (test_user2_id_ > 0) {
        UserDAO::Delete(*conn_guard_, test_user2_id_);
      }
      if (test_user_id_ > 0) {
        UserDAO::Delete(*conn_guard_, test_user_id_);
      }
    }
  }

  // 创建测试提交，自动跟踪 ID 用于清理
  int64_t create_submission(int64_t user_id, int64_t problem_id,
                             const std::string& code = "#include <iostream>\nint main() { return 0; }") {
    Submission sub;
    sub.user_id = user_id;
    sub.problem_id = problem_id;
    sub.code = code;
    sub.status = SubmissionStatus::pending;

    int64_t sid = SubmissionDAO::Create(*conn_guard_, sub);
    if (sid > 0) created_submission_ids_.push_back(sid);
    return sid;
  }

  ServerConfig cfg_;
  ConnectionGuard conn_guard_;
  int64_t test_user_id_ = 0;
  int64_t test_user2_id_ = 0;
  int64_t test_problem_id_ = 0;
  std::vector<int64_t> created_submission_ids_;

  std::unique_ptr<EnvGuard> env_db_host_;
  std::unique_ptr<EnvGuard> env_db_name_;
};

// ═══════════════════════════════════════════════════════════════
// POST /api/v1/submissions — 创建提交
// ═══════════════════════════════════════════════════════════════

// ── 成功创建提交 ──────────────────────────────────────────────
TEST_F(SubmissionAPITest, CreateSuccess) {
  int64_t sid = create_submission(test_user_id_, test_problem_id_,
                                   "#include <iostream>\nint main() { return 0; }");
  ASSERT_GT(sid, 0);

  auto sub = SubmissionDAO::FindById(*conn_guard_, sid);
  ASSERT_TRUE(sub.has_value());
  EXPECT_EQ(sub->user_id, test_user_id_);
  EXPECT_EQ(sub->problem_id, test_problem_id_);
  EXPECT_EQ(sub->status, SubmissionStatus::pending);
  EXPECT_FALSE(sub->code.empty());
  EXPECT_FALSE(sub->created_at.empty());
  EXPECT_FALSE(sub->updated_at.empty());
}

// ── 创建提交时默认状态为 pending ────────────────────────────────
TEST_F(SubmissionAPITest, CreateDefaultStatusIsPending) {
  int64_t sid = create_submission(test_user_id_, test_problem_id_);
  ASSERT_GT(sid, 0);

  auto sub = SubmissionDAO::FindById(*conn_guard_, sid);
  ASSERT_TRUE(sub.has_value());
  EXPECT_EQ(sub->status, SubmissionStatus::pending);
}

// ── 创建提交后 passed_cases / total_cases 默认为 0 ──────────────
TEST_F(SubmissionAPITest, CreateDefaultCountsAreZero) {
  int64_t sid = create_submission(test_user_id_, test_problem_id_);
  ASSERT_GT(sid, 0);

  auto sub = SubmissionDAO::FindById(*conn_guard_, sid);
  ASSERT_TRUE(sub.has_value());
  EXPECT_EQ(sub->passed_cases, 0);
  EXPECT_EQ(sub->total_cases, 0);
}

// ── 相同用户可多次提交同一题目 ──────────────────────────────────
TEST_F(SubmissionAPITest, MultipleSubmissionsSameProblem) {
  int64_t sid1 = create_submission(test_user_id_, test_problem_id_, "code A");
  int64_t sid2 = create_submission(test_user_id_, test_problem_id_, "code B");
  ASSERT_GT(sid1, 0);
  ASSERT_GT(sid2, 0);
  EXPECT_NE(sid1, sid2);

  auto sub1 = SubmissionDAO::FindById(*conn_guard_, sid1);
  auto sub2 = SubmissionDAO::FindById(*conn_guard_, sid2);
  ASSERT_TRUE(sub1.has_value());
  ASSERT_TRUE(sub2.has_value());
  EXPECT_EQ(sub1->code, "code A");
  EXPECT_EQ(sub2->code, "code B");
}

// ── 不同用户可提交同一题目 ──────────────────────────────────────
TEST_F(SubmissionAPITest, DifferentUsersSameProblem) {
  int64_t sid1 = create_submission(test_user_id_, test_problem_id_, "user1 code");
  int64_t sid2 = create_submission(test_user2_id_, test_problem_id_, "user2 code");
  ASSERT_GT(sid1, 0);
  ASSERT_GT(sid2, 0);

  auto sub1 = SubmissionDAO::FindById(*conn_guard_, sid1);
  auto sub2 = SubmissionDAO::FindById(*conn_guard_, sid2);
  ASSERT_TRUE(sub1.has_value());
  ASSERT_TRUE(sub2.has_value());
  EXPECT_EQ(sub1->user_id, test_user_id_);
  EXPECT_EQ(sub2->user_id, test_user2_id_);
}

// ── 长代码提交 ──────────────────────────────────────────────────
TEST_F(SubmissionAPITest, LongCodeSubmission) {
  std::string long_code = "#include <iostream>\nint main() {\n";
  for (int i = 0; i < 100; i++) {
    long_code += "  std::cout << \"" + std::to_string(i) + "\\n\";\n";
  }
  long_code += "  return 0;\n}\n";

  int64_t sid = create_submission(test_user_id_, test_problem_id_, long_code);
  ASSERT_GT(sid, 0);

  auto sub = SubmissionDAO::FindById(*conn_guard_, sid);
  ASSERT_TRUE(sub.has_value());
  EXPECT_EQ(sub->code, long_code);
}

// ═══════════════════════════════════════════════════════════════
// GET /api/v1/submissions/:id — 查询单条提交
// ═══════════════════════════════════════════════════════════════

// ── 查询存在的提交 ─────────────────────────────────────────────
TEST_F(SubmissionAPITest, FindByIdSuccess) {
  int64_t sid = create_submission(test_user_id_, test_problem_id_);
  ASSERT_GT(sid, 0);

  auto sub = SubmissionDAO::FindById(*conn_guard_, sid);
  ASSERT_TRUE(sub.has_value());
  EXPECT_EQ(sub->id, sid);
  EXPECT_EQ(sub->user_id, test_user_id_);
  EXPECT_EQ(sub->problem_id, test_problem_id_);
}

// ── 查询不存在的提交返回 std::nullopt ──────────────────────────
TEST_F(SubmissionAPITest, FindByIdNotFound) {
  auto sub = SubmissionDAO::FindById(*conn_guard_, 99999999);
  EXPECT_FALSE(sub.has_value());
}

// ── 查询返回完整字段 ──────────────────────────────────────────
TEST_F(SubmissionAPITest, FindByIdAllFields) {
  int64_t sid = create_submission(test_user_id_, test_problem_id_,
                                   "// complete field test");
  ASSERT_GT(sid, 0);

  auto sub = SubmissionDAO::FindById(*conn_guard_, sid);
  ASSERT_TRUE(sub.has_value());
  EXPECT_GT(sub->id, 0);
  EXPECT_EQ(sub->user_id, test_user_id_);
  EXPECT_EQ(sub->problem_id, test_problem_id_);
  EXPECT_EQ(sub->code, "// complete field test");
  EXPECT_EQ(sub->status, SubmissionStatus::pending);
  EXPECT_EQ(sub->passed_cases, 0);
  EXPECT_EQ(sub->total_cases, 0);
  EXPECT_EQ(sub->time_used_ms, 0);
  EXPECT_EQ(sub->memory_used_kb, 0);
  EXPECT_FALSE(sub->created_at.empty());
  EXPECT_FALSE(sub->updated_at.empty());
}

// ═══════════════════════════════════════════════════════════════
// SubmissionDAO::List — 分页列表查询
// ═══════════════════════════════════════════════════════════════

// ── 空列表 ─────────────────────────────────────────────────────
TEST_F(SubmissionAPITest, ListEmpty) {
  auto result = SubmissionDAO::List(*conn_guard_, test_user_id_);
  EXPECT_EQ(result.total, 0);
  EXPECT_EQ(static_cast<int>(result.submissions.size()), 0);
}

// ── 按 user_id 筛选 ────────────────────────────────────────────
TEST_F(SubmissionAPITest, ListByUserId) {
  create_submission(test_user_id_, test_problem_id_, "code1");
  create_submission(test_user_id_, test_problem_id_, "code2");
  create_submission(test_user2_id_, test_problem_id_, "other user");

  auto result = SubmissionDAO::List(*conn_guard_, test_user_id_);
  EXPECT_EQ(result.total, 2);
  EXPECT_EQ(static_cast<int>(result.submissions.size()), 2);
  for (const auto& s : result.submissions) {
    EXPECT_EQ(s.user_id, test_user_id_);
  }
}

// ── 按 problem_id 筛选 ─────────────────────────────────────────
TEST_F(SubmissionAPITest, ListByProblemId) {
  // 需要再创建一个题目来区分
  Problem p2;
  p2.title = "Second Problem";
  p2.description = "Another problem";
  p2.difficulty = Difficulty::medium;
  p2.time_limit_ms = 2000;
  p2.memory_limit_kb = 131072;
  p2.created_by = test_user_id_;
  int64_t pid2 = ProblemDAO::Create(*conn_guard_, p2);
  ASSERT_GT(pid2, 0);

  create_submission(test_user_id_, test_problem_id_, "code A");
  create_submission(test_user_id_, pid2, "code B");

  auto result = SubmissionDAO::List(*conn_guard_, 0, test_problem_id_);
  EXPECT_EQ(result.total, 1);
  ASSERT_EQ(static_cast<int>(result.submissions.size()), 1);
  EXPECT_EQ(result.submissions[0].problem_id, test_problem_id_);

  // 清理第二个题目
  ProblemDAO::Delete(*conn_guard_, pid2);
}

// ── 分页：第 1 页 ──────────────────────────────────────────────
TEST_F(SubmissionAPITest, ListPaginationPage1) {
  for (int i = 1; i <= 5; i++) {
    create_submission(test_user_id_, test_problem_id_,
                      "code " + std::to_string(i));
  }

  auto result = SubmissionDAO::List(*conn_guard_, test_user_id_, 0, 1, 2);
  EXPECT_EQ(result.total, 5);
  EXPECT_EQ(static_cast<int>(result.submissions.size()), 2);
}

// ── 分页：第 3 页（最后 1 条） ──────────────────────────────────
TEST_F(SubmissionAPITest, ListPaginationLastPage) {
  for (int i = 1; i <= 5; i++) {
    create_submission(test_user_id_, test_problem_id_,
                      "code " + std::to_string(i));
  }

  auto result = SubmissionDAO::List(*conn_guard_, test_user_id_, 0, 3, 2);
  EXPECT_EQ(result.total, 5);
  EXPECT_EQ(static_cast<int>(result.submissions.size()), 1);
}

// ── 分页：超出范围的页码返回空列表 ──────────────────────────────
TEST_F(SubmissionAPITest, ListPageOutOfRange) {
  create_submission(test_user_id_, test_problem_id_);

  auto result = SubmissionDAO::List(*conn_guard_, test_user_id_, 0, 999, 20);
  EXPECT_GE(result.total, 1);
  EXPECT_EQ(static_cast<int>(result.submissions.size()), 0);
}

// ── 列表按 id 降序排列（最新提交在前） ──────────────────────────
TEST_F(SubmissionAPITest, ListOrderByIdDesc) {
  int64_t sid1 = create_submission(test_user_id_, test_problem_id_, "first");
  int64_t sid2 = create_submission(test_user_id_, test_problem_id_, "second");

  auto result = SubmissionDAO::List(*conn_guard_, test_user_id_);
  ASSERT_GE(static_cast<int>(result.submissions.size()), 2);
  // 最新提交（id 较大）应在前面
  EXPECT_GE(result.submissions[0].id, result.submissions[1].id);
}

// ── 同时按 user_id + problem_id 筛选 ───────────────────────────
TEST_F(SubmissionAPITest, ListByUserAndProblem) {
  create_submission(test_user_id_, test_problem_id_, "user1-p1");
  create_submission(test_user2_id_, test_problem_id_, "user2-p1");

  auto result = SubmissionDAO::List(*conn_guard_, test_user_id_,
                                    test_problem_id_);
  EXPECT_EQ(result.total, 1);
  ASSERT_EQ(static_cast<int>(result.submissions.size()), 1);
  EXPECT_EQ(result.submissions[0].user_id, test_user_id_);
  EXPECT_EQ(result.submissions[0].problem_id, test_problem_id_);
}

// ── 不带任何筛选查看全部（admin 场景） ──────────────────────────
TEST_F(SubmissionAPITest, ListAllNoFilter) {
  create_submission(test_user_id_, test_problem_id_, "all test 1");
  create_submission(test_user2_id_, test_problem_id_, "all test 2");

  auto result = SubmissionDAO::List(*conn_guard_);
  EXPECT_GE(result.total, 2);
  EXPECT_GE(static_cast<int>(result.submissions.size()), 2);
}

// ═══════════════════════════════════════════════════════════════
// SubmissionDAO::UpdateStatus — 更新判题状态
// ═══════════════════════════════════════════════════════════════

// ── 更新为 accepted ─────────────────────────────────────────────
TEST_F(SubmissionAPITest, UpdateStatusAccepted) {
  int64_t sid = create_submission(test_user_id_, test_problem_id_);
  ASSERT_GT(sid, 0);

  bool ok = SubmissionDAO::UpdateStatus(*conn_guard_, sid,
      SubmissionStatus::accepted, 5, 5, 120, 50000);
  EXPECT_TRUE(ok);

  auto sub = SubmissionDAO::FindById(*conn_guard_, sid);
  ASSERT_TRUE(sub.has_value());
  EXPECT_EQ(sub->status, SubmissionStatus::accepted);
  EXPECT_EQ(sub->passed_cases, 5);
  EXPECT_EQ(sub->total_cases, 5);
  EXPECT_EQ(sub->time_used_ms, 120);
  EXPECT_EQ(sub->memory_used_kb, 50000);
}

// ── 更新为 compile_error（含编译输出） ──────────────────────────
TEST_F(SubmissionAPITest, UpdateStatusCompileError) {
  int64_t sid = create_submission(test_user_id_, test_problem_id_);
  ASSERT_GT(sid, 0);

  bool ok = SubmissionDAO::UpdateStatus(*conn_guard_, sid,
      SubmissionStatus::compile_error, 0, 5, 0, 0,
      "error: expected ';' before '}'");
  EXPECT_TRUE(ok);

  auto sub = SubmissionDAO::FindById(*conn_guard_, sid);
  ASSERT_TRUE(sub.has_value());
  EXPECT_EQ(sub->status, SubmissionStatus::compile_error);
  EXPECT_EQ(sub->compile_output, "error: expected ';' before '}'");
  EXPECT_EQ(sub->passed_cases, 0);
  EXPECT_EQ(sub->total_cases, 5);
}

// ── 更新为 wrong_answer（含 diff 输出） ─────────────────────────
TEST_F(SubmissionAPITest, UpdateStatusWrongAnswer) {
  int64_t sid = create_submission(test_user_id_, test_problem_id_);
  ASSERT_GT(sid, 0);

  bool ok = SubmissionDAO::UpdateStatus(*conn_guard_, sid,
      SubmissionStatus::wrong_answer, 2, 5, 150, 40000, "",
      "-expected\n+actual");
  EXPECT_TRUE(ok);

  auto sub = SubmissionDAO::FindById(*conn_guard_, sid);
  ASSERT_TRUE(sub.has_value());
  EXPECT_EQ(sub->status, SubmissionStatus::wrong_answer);
  EXPECT_EQ(sub->passed_cases, 2);
  EXPECT_EQ(sub->total_cases, 5);
  EXPECT_EQ(sub->diff_output, "-expected\n+actual");
}

// ── 更新不存在的提交返回 false ──────────────────────────────────
TEST_F(SubmissionAPITest, UpdateStatusNotFound) {
  bool ok = SubmissionDAO::UpdateStatus(*conn_guard_, 99999999,
      SubmissionStatus::accepted);
  EXPECT_FALSE(ok);
}

// ═══════════════════════════════════════════════════════════════
// SubmissionDAO::Delete — 删除提交
// ═══════════════════════════════════════════════════════════════

// ── 删除存在的提交 ─────────────────────────────────────────────
TEST_F(SubmissionAPITest, DeleteSuccess) {
  int64_t sid = create_submission(test_user_id_, test_problem_id_);
  ASSERT_GT(sid, 0);

  bool ok = SubmissionDAO::Delete(*conn_guard_, sid);
  EXPECT_TRUE(ok);

  // 确认已删除
  auto sub = SubmissionDAO::FindById(*conn_guard_, sid);
  EXPECT_FALSE(sub.has_value());

  // 从清理列表移除（已手动删除）
  created_submission_ids_.erase(
      std::remove(created_submission_ids_.begin(),
                  created_submission_ids_.end(), sid),
      created_submission_ids_.end());
}

// ── 删除不存在的提交返回 false ──────────────────────────────────
TEST_F(SubmissionAPITest, DeleteNotFound) {
  bool ok = SubmissionDAO::Delete(*conn_guard_, 99999999);
  EXPECT_FALSE(ok);
}

// ═══════════════════════════════════════════════════════════════
// 状态转换测试
// ═══════════════════════════════════════════════════════════════

// ── 验证所有状态枚举值都能正确转换 ───────────────────────────────
TEST_F(SubmissionAPITest, StatusRoundTrip) {
  std::vector<SubmissionStatus> all_statuses = {
    SubmissionStatus::pending,
    SubmissionStatus::compiling,
    SubmissionStatus::running,
    SubmissionStatus::accepted,
    SubmissionStatus::wrong_answer,
    SubmissionStatus::time_limit,
    SubmissionStatus::memory_limit,
    SubmissionStatus::runtime_error,
    SubmissionStatus::compile_error,
    SubmissionStatus::system_error
  };

  for (auto s : all_statuses) {
    std::string str = submission_status_to_string(s);
    EXPECT_FALSE(str.empty());
    SubmissionStatus back = submission_status_from_string(str);
    EXPECT_EQ(s, back) << "Round-trip failed for status: " << str;
  }
}

// ── 非法状态字符串返回 pending ──────────────────────────────────
TEST_F(SubmissionAPITest, StatusFromInvalidString) {
  EXPECT_EQ(submission_status_from_string(""), SubmissionStatus::pending);
  EXPECT_EQ(submission_status_from_string("invalid"), SubmissionStatus::pending);
  EXPECT_EQ(submission_status_from_string("Accepted"), SubmissionStatus::pending);
}

}  // namespace
}  // namespace vibeoj
