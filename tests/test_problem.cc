// 题目列表/详情 API 单元测试 — 测试 GET /api/v1/problems（列表+筛选+分页）
// 与 GET /api/v1/problems/:id（详情+样例）。
//
// 需要本地 MySQL 运行（oj_system 数据库）。测试通过 DAO 层直接验证业务逻辑。
// 所有测试数据在完成后自动清理。

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "common/log.h"
#include "config/config.h"
#include "db/connection_pool.h"
#include "db/dao.h"
#include "model/problem.h"
#include "model/user.h"
#include "nlohmann/json.hpp"

namespace vibeoj {
namespace {

using json = nlohmann::json;

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

// ── 测试夹具 — 每个 TEST_F 前后自动调用 SetUp/TearDown ────────
class ProblemAPITest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 设置环境变量（若测试环境未设置）
    if (!std::getenv("VIBEOJ_DB_HOST")) {
      env_db_host_ = std::make_unique<EnvGuard>("VIBEOJ_DB_HOST", "localhost");
    }
    if (!std::getenv("VIBEOJ_DB_NAME")) {
      env_db_name_ = std::make_unique<EnvGuard>("VIBEOJ_DB_NAME", "oj_system");
    }
    // 初始化日志到临时目录
    Logger::instance().init("/tmp/vibeoj_test_logs");

    // 加载配置并初始化连接池
    cfg_ = load_config();
    ConnectionPool::instance().init(cfg_);

    // 预先获取一个连接用于数据准备和清理
    conn_guard_ = ConnectionPool::instance().acquire();

    // 创建测试用户（problems.created_by 需要引用有效用户）
    test_user_id_ = UserDAO::Create(*conn_guard_, "_test_problem_user",
                                     "$2b$12$dummyhashdummyhashdummyhashdummy");
    if (test_user_id_ <= 0) {
      // 用户可能已存在（前次测试异常退出未清理），尝试查找
      auto existing = UserDAO::FindByUsername(*conn_guard_, "_test_problem_user");
      if (existing.has_value()) {
        test_user_id_ = existing->id;
      }
    }
  }

  void TearDown() override {
    // 清理测试数据（逆序：先删 test_cases，再删 problems，最后删用户）
    if (conn_guard_) {
      for (int64_t pid : created_problem_ids_) {
        TestCaseDAO::DeleteByProblemId(*conn_guard_, pid);
        ProblemDAO::Delete(*conn_guard_, pid);
      }
      if (test_user_id_ > 0) {
        UserDAO::Delete(*conn_guard_, test_user_id_);
      }
    }
  }

  int64_t create_test_problem(const std::string& title = "Test Problem",
                               Difficulty diff = Difficulty::easy,
                               const std::string& desc = "Problem description") {
    Problem p;
    p.title = title;
    p.description = desc;
    p.difficulty = diff;
    p.time_limit_ms = 1000;
    p.memory_limit_kb = 262144;
    p.created_by = test_user_id_;

    int64_t pid = ProblemDAO::Create(*conn_guard_, p);
    if (pid > 0) created_problem_ids_.push_back(pid);
    return pid;
  }

  int64_t create_test_case(int64_t problem_id, const std::string& input,
                            const std::string& expected, bool is_sample = true,
                            int order = 1) {
    TestCase tc;
    tc.problem_id = problem_id;
    tc.input = input;
    tc.expected_output = expected;
    tc.is_sample = is_sample;
    tc.order_index = order;
    return TestCaseDAO::Create(*conn_guard_, tc);
  }

  ServerConfig cfg_;
  ConnectionGuard conn_guard_;
  int64_t test_user_id_ = 0;
  std::vector<int64_t> created_problem_ids_;

  std::unique_ptr<EnvGuard> env_db_host_;
  std::unique_ptr<EnvGuard> env_db_name_;
};

// ── 测试：题目列表（空数据库返回空列表） ───────────────────────
TEST_F(ProblemAPITest, ListEmpty) {
  auto result = ProblemDAO::FindAll(*conn_guard_);
  // 任何情况下 total >= 0
  EXPECT_GE(result.total, 0);
  // items 应为空
  EXPECT_EQ(result.total, 0);
}

// ── 测试：题目列表（有数据时返回所有题目） ─────────────────────
TEST_F(ProblemAPITest, ListReturnsAllProblems) {
  create_test_problem("Two Sum", Difficulty::easy);
  create_test_problem("Three Sum", Difficulty::medium);
  create_test_problem("N-Queens", Difficulty::hard);

  auto result = ProblemDAO::FindAll(*conn_guard_);
  EXPECT_GE(result.total, 3);
  EXPECT_GE(static_cast<int>(result.problems.size()), 3);
  // 按 id 升序排列
  EXPECT_LE(result.problems.front().id, result.problems.back().id);
}

// ── 测试：题目列表 — 难度筛选 ──────────────────────────────────
TEST_F(ProblemAPITest, ListFilterByDifficulty) {
  create_test_problem("Easy One", Difficulty::easy);
  create_test_problem("Medium One", Difficulty::medium);
  create_test_problem("Hard One", Difficulty::hard);

  // 筛选 easy
  auto easy = ProblemDAO::FindAll(*conn_guard_, "easy");
  for (const auto& p : easy.problems) {
    EXPECT_EQ(p.difficulty, Difficulty::easy);
  }
  EXPECT_GE(easy.total, 1);

  // 筛选 hard
  auto hard = ProblemDAO::FindAll(*conn_guard_, "hard");
  for (const auto& p : hard.problems) {
    EXPECT_EQ(p.difficulty, Difficulty::hard);
  }
  EXPECT_GE(hard.total, 1);
}

// ── 测试：题目列表 — 非法难度值返回空结果 ──────────────────────
TEST_F(ProblemAPITest, ListInvalidDifficulty) {
  create_test_problem("Easy One", Difficulty::easy);
  // 非法难度值不会匹配任何题目
  auto result = ProblemDAO::FindAll(*conn_guard_, "invalid_difficulty");
  EXPECT_EQ(result.total, 0);
}

// ── 测试：题目列表 — 分页 ────────────────────────────────────
TEST_F(ProblemAPITest, ListPagination) {
  for (int i = 1; i <= 5; i++) {
    create_test_problem("Problem " + std::to_string(i));
  }

  // 第 1 页，每页 2 条
  auto page1 = ProblemDAO::FindAll(*conn_guard_, "", 1, 2);
  EXPECT_EQ(static_cast<int>(page1.problems.size()), 2);
  EXPECT_GE(page1.total, 5);

  // 第 3 页，每页 2 条（应只剩 1 条）
  auto page3 = ProblemDAO::FindAll(*conn_guard_, "", 3, 2);
  EXPECT_EQ(static_cast<int>(page3.problems.size()), 1);
}

// ── 测试：题目列表 — page_size 超上限的效果由 handler 层处理 ──
TEST_F(ProblemAPITest, ListLargePageSize) {
  for (int i = 1; i <= 3; i++) {
    create_test_problem("P" + std::to_string(i));
  }
  // DAO 层不做 page_size 上限限制（handler 层负责）
  auto result = ProblemDAO::FindAll(*conn_guard_, "", 1, 5);
  EXPECT_EQ(static_cast<int>(result.problems.size()), 3);
  EXPECT_EQ(result.total, 3);
}

// ── 测试：题目详情 — 正确返回题目信息 ──────────────────────────
TEST_F(ProblemAPITest, DetailSuccess) {
  int64_t pid = create_test_problem("Two Sum", Difficulty::easy,
                                     "Find two numbers that sum to target.");
  ASSERT_GT(pid, 0);

  create_test_case(pid, "3\n2 7 11 15\n9\n", "0 1\n", true, 1);
  create_test_case(pid, "2\n3 3\n6\n", "0 1\n", false, 2);

  auto problem = ProblemDAO::FindById(*conn_guard_, pid);
  ASSERT_TRUE(problem.has_value());
  EXPECT_EQ(problem->title, "Two Sum");
  EXPECT_EQ(problem->description, "Find two numbers that sum to target.");
  EXPECT_EQ(problem->difficulty, Difficulty::easy);
  EXPECT_EQ(problem->time_limit_ms, 1000);
  EXPECT_EQ(problem->memory_limit_kb, 262144);
  EXPECT_FALSE(problem->created_at.empty());
}

// ── 测试：题目详情 — 不存在的题目返回 std::nullopt ────────────
TEST_F(ProblemAPITest, DetailNotFound) {
  auto problem = ProblemDAO::FindById(*conn_guard_, 99999999);
  EXPECT_FALSE(problem.has_value());
}

// ── 测试：测试用例 — is_sample 过滤 ──────────────────────────
TEST_F(ProblemAPITest, TestCaseSampleFiltering) {
  int64_t pid = create_test_problem("Sample Test");
  ASSERT_GT(pid, 0);

  create_test_case(pid, "input1", "output1", true, 1);
  create_test_case(pid, "input2", "output2", true, 2);
  create_test_case(pid, "input3", "output3", false, 3);  // 隐藏用例

  auto all_cases = TestCaseDAO::FindByProblemId(*conn_guard_, pid);
  EXPECT_EQ(static_cast<int>(all_cases.size()), 3);

  // 过滤 is_sample
  int sample_count = 0, hidden_count = 0;
  for (const auto& tc : all_cases) {
    if (tc.is_sample) sample_count++;
    else hidden_count++;
  }
  EXPECT_EQ(sample_count, 2);
  EXPECT_EQ(hidden_count, 1);
}

// ── 测试：题目列表 — 摘要 JSON 格式验证 ─────────────────────────
TEST_F(ProblemAPITest, SummaryJsonFormat) {
  int64_t pid = create_test_problem("JSON Test", Difficulty::medium);
  ASSERT_GT(pid, 0);

  auto problem = ProblemDAO::FindById(*conn_guard_, pid);
  ASSERT_TRUE(problem.has_value());

  // 列表摘要不含 description 和 test_cases
  json j;
  j["id"] = problem->id;
  j["title"] = problem->title;
  j["difficulty"] = difficulty_to_string(problem->difficulty);
  j["created_at"] = problem->created_at;

  EXPECT_EQ(j["id"].get<int64_t>(), pid);
  EXPECT_EQ(j["title"].get<std::string>(), "JSON Test");
  EXPECT_EQ(j["difficulty"].get<std::string>(), "medium");
  EXPECT_FALSE(j["created_at"].get<std::string>().empty());
}

// ── 测试：题目详情 — 完整 JSON 格式验证 ─────────────────────────
TEST_F(ProblemAPITest, DetailJsonFormat) {
  int64_t pid = create_test_problem("Detail JSON", Difficulty::hard,
                                     "Markdown description here.");
  ASSERT_GT(pid, 0);

  create_test_case(pid, "in1", "out1", true, 1);
  create_test_case(pid, "in2", "out2", false, 2);

  auto problem = ProblemDAO::FindById(*conn_guard_, pid);
  ASSERT_TRUE(problem.has_value());

  json j;
  j["id"] = problem->id;
  j["title"] = problem->title;
  j["description"] = problem->description;
  j["difficulty"] = difficulty_to_string(problem->difficulty);
  j["time_limit_ms"] = problem->time_limit_ms;
  j["memory_limit_kb"] = problem->memory_limit_kb;
  j["created_at"] = problem->created_at;

  auto cases = TestCaseDAO::FindByProblemId(*conn_guard_, pid);
  json samples = json::array();
  for (const auto& tc : cases) {
    if (tc.is_sample) {
      samples.push_back({{"id", tc.id}, {"input", tc.input},
                         {"expected_output", tc.expected_output},
                         {"order_index", tc.order_index}});
    }
  }
  j["sample_cases"] = samples;

  EXPECT_EQ(j["id"].get<int64_t>(), pid);
  EXPECT_EQ(j["title"].get<std::string>(), "Detail JSON");
  EXPECT_EQ(j["description"].get<std::string>(), "Markdown description here.");
  EXPECT_EQ(j["difficulty"].get<std::string>(), "hard");
  EXPECT_EQ(j["time_limit_ms"].get<int>(), 1000);
  EXPECT_EQ(j["memory_limit_kb"].get<int>(), 262144);
  EXPECT_EQ(j["sample_cases"].size(), 1u);  // 仅 is_sample=true
  EXPECT_EQ(j["sample_cases"][0]["input"].get<std::string>(), "in1");
}

// ── 测试：题目列表 — 难度筛选 + 分页组合 ───────────────────────
TEST_F(ProblemAPITest, ListFilterAndPaginate) {
  // 创建 4 个 easy 题目
  for (int i = 1; i <= 4; i++) {
    create_test_problem("Easy " + std::to_string(i), Difficulty::easy);
  }
  // 创建 2 个 hard 题目
  for (int i = 1; i <= 2; i++) {
    create_test_problem("Hard " + std::to_string(i), Difficulty::hard);
  }

  // 筛选 easy，page_size=2，第 2 页应有 2 条
  auto page2 = ProblemDAO::FindAll(*conn_guard_, "easy", 2, 2);
  EXPECT_EQ(static_cast<int>(page2.problems.size()), 2);
  EXPECT_GE(page2.total, 4);
  for (const auto& p : page2.problems) {
    EXPECT_EQ(p.difficulty, Difficulty::easy);
  }
}

// ── 测试：题目列表 — 超出范围的页码返回空列表 ──────────────────
TEST_F(ProblemAPITest, ListPageOutOfRange) {
  create_test_problem("Only One");
  auto result = ProblemDAO::FindAll(*conn_guard_, "", 999, 20);
  EXPECT_EQ(static_cast<int>(result.problems.size()), 0);
  EXPECT_GE(result.total, 1);  // total 仍反映实际记录数
}

}  // namespace
}  // namespace vibeoj
