// 管理后台 API 单元测试 — 测试管理后台所有端点。
//
// 需要本地 MySQL 运行（oj_system 数据库）。启动本地 HTTP 服务器进行集成测试。
// 所有测试数据在完成后自动清理。

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "auth/jwt.h"
#include "auth/middleware.h"
#include "common/log.h"
#include "config/config.h"
#include "db/connection_pool.h"
#include "db/dao.h"
#include "handler/admin_handler.h"
#include "handler/auth_handler.h"
#include "model/problem.h"
#include "model/user.h"

#include "httplib.h"
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

// ── 管理后台 API 集成测试夹具 ──────────────────────────────────
class AdminAPITest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    Logger::instance().init("logs");
    ServerConfig cfg = load_config();
    jwt_secret_ = cfg.jwt_secret;

    ConnectionPool::instance().init(cfg);

    // 使用随机端口避免冲突
    cfg.port = 18081;
    server_ = std::make_unique<httplib::Server>();

    // 注册管理后台路由
    vibeoj::AdminHandler::register_routes(*server_, cfg);

    server_thread_ = std::thread([cfg]() {
      server_->listen("127.0.0.1", cfg.port);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    client_ = std::make_unique<httplib::Client>("http://127.0.0.1:18081");

    // 创建 admin 用户和普通用户（通过 DAO，不走 API）
    {
      auto guard = ConnectionPool::instance().acquire();
      std::string hash = "$2b$12$testhashfortestadminuser123456789";
      admin_id_ = UserDAO::Create(*guard, "_test_admin_user", hash,
                                  UserRole::admin);
      if (admin_id_ <= 0) {
        auto u = UserDAO::FindByUsername(*guard, "_test_admin_user");
        if (u.has_value()) admin_id_ = u->id;
      }

      user_id_ = UserDAO::Create(*guard, "_test_normal_user", hash,
                                 UserRole::user);
      if (user_id_ <= 0) {
        auto u = UserDAO::FindByUsername(*guard, "_test_normal_user");
        if (u.has_value()) user_id_ = u->id;
      }
    }
  }

  static void TearDownTestSuite() {
    // 清理测试用户
    {
      auto guard = ConnectionPool::instance().acquire();
      if (guard) {
        if (admin_id_ > 0) UserDAO::Delete(*guard, admin_id_);
        if (user_id_ > 0) UserDAO::Delete(*guard, user_id_);
      }
    }

    server_->stop();
    if (server_thread_.joinable()) server_thread_.join();
    client_.reset();
    server_.reset();
    ConnectionPool::instance().shutdown();
    Logger::instance().close();
  }

  void SetUp() override {
    // 每个测试前清理题目数据
    cleanup_problems();
  }

  void TearDown() override { cleanup_problems(); }

  void cleanup_problems() {
    for (auto pid : created_problem_ids_) {
      auto guard = ConnectionPool::instance().acquire();
      if (guard) {
        TestCaseDAO::DeleteByProblemId(*guard, pid);
        ProblemDAO::Delete(*guard, pid);
      }
    }
    created_problem_ids_.clear();
    created_testcase_ids_.clear();
  }

  // 生成 admin JWT token
  std::string admin_token() {
    User admin;
    admin.id = admin_id_;
    admin.username = "_test_admin_user";
    admin.role = UserRole::admin;
    return generate_access_token(admin, jwt_secret_);
  }

  // 生成普通用户 JWT token
  std::string user_token() {
    User user;
    user.id = user_id_;
    user.username = "_test_normal_user";
    user.role = UserRole::user;
    return generate_access_token(user, jwt_secret_);
  }

  // 带 admin 认证的请求头
  httplib::Headers admin_auth() {
    return {{"Authorization", "Bearer " + admin_token()}};
  }

  // 带普通用户认证的请求头
  httplib::Headers user_auth() {
    return {{"Authorization", "Bearer " + user_token()}};
  }

  // 通过 API 创建题目，返回 problem_id
  int64_t create_problem_via_api(const std::string& title = "Test Problem",
                                  const std::string& difficulty = "easy") {
    json body = {{"title", title},
                 {"description", "A test problem description."},
                 {"difficulty", difficulty},
                 {"time_limit_ms", 1000},
                 {"memory_limit_kb", 262144}};
    auto res = client_->Post("/api/v1/admin/problems", admin_auth(),
                             body.dump(), "application/json");
    if (res && res->status == 201) {
      auto j = json::parse(res->body);
      int64_t pid = j["data"]["id"].get<int64_t>();
      created_problem_ids_.push_back(pid);
      return pid;
    }
    return -1;
  }

  // 通过 API 创建测试用例
  int64_t create_testcase_via_api(int64_t problem_id,
                                   const std::string& input = "1 2",
                                   const std::string& expected = "3",
                                   bool is_sample = true, int order = 1) {
    json body = {{"input", input},
                 {"expected_output", expected},
                 {"is_sample", is_sample},
                 {"order_index", order}};
    auto res = client_->Post(
        "/api/v1/admin/problems/" + std::to_string(problem_id) + "/testcases",
        admin_auth(), body.dump(), "application/json");
    if (res && res->status == 201) {
      auto j = json::parse(res->body);
      int64_t tcid = j["data"]["id"].get<int64_t>();
      created_testcase_ids_.push_back(tcid);
      return tcid;
    }
    return -1;
  }

  static std::unique_ptr<httplib::Server> server_;
  static std::unique_ptr<httplib::Client> client_;
  static std::thread server_thread_;
  static std::string jwt_secret_;
  static int64_t admin_id_;
  static int64_t user_id_;

  std::vector<int64_t> created_problem_ids_;
  std::vector<int64_t> created_testcase_ids_;
};

std::unique_ptr<httplib::Server> AdminAPITest::server_;
std::unique_ptr<httplib::Client> AdminAPITest::client_;
std::thread AdminAPITest::server_thread_;
std::string AdminAPITest::jwt_secret_;
int64_t AdminAPITest::admin_id_ = 0;
int64_t AdminAPITest::user_id_ = 0;

// ═══════════════════════════════════════════════════════════════
// 认证与授权测试
// ═══════════════════════════════════════════════════════════════

TEST_F(AdminAPITest, NoAuthReturns401) {
  auto res = client_->Get("/api/v1/admin/users");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 401);
}

TEST_F(AdminAPITest, NormalUserReturns403) {
  auto res = client_->Get("/api/v1/admin/users", user_auth());
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 403);
}

TEST_F(AdminAPITest, AdminAccessAllowed) {
  auto res = client_->Get("/api/v1/admin/users", admin_auth());
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 200);
}

// ═══════════════════════════════════════════════════════════════
// POST /api/v1/admin/problems — 创建题目
// ═══════════════════════════════════════════════════════════════

TEST_F(AdminAPITest, CreateProblemSuccess) {
  json body = {{"title", "Two Sum"},
               {"description", "Find two numbers that sum to target."},
               {"difficulty", "easy"},
               {"time_limit_ms", 1000},
               {"memory_limit_kb", 262144}};
  auto res = client_->Post("/api/v1/admin/problems", admin_auth(), body.dump(),
                           "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 201);

  auto j = json::parse(res->body);
  EXPECT_EQ(j["data"]["title"], "Two Sum");
  EXPECT_EQ(j["data"]["difficulty"], "easy");
  EXPECT_GT(j["data"]["id"].get<int64_t>(), 0);
  EXPECT_FALSE(j["data"]["created_at"].get<std::string>().empty());

  created_problem_ids_.push_back(j["data"]["id"].get<int64_t>());
}

TEST_F(AdminAPITest, CreateProblemWithTestCases) {
  json body = {{"title", "Problem With Cases"},
               {"description", "Has test cases."},
               {"difficulty", "medium"},
               {"test_cases",
                {{{"input", "1 2"},
                  {"expected_output", "3"},
                  {"is_sample", true},
                  {"order_index", 1}},
                 {{"input", "4 5"},
                  {"expected_output", "9"},
                  {"is_sample", false},
                  {"order_index", 2}}}}};
  auto res = client_->Post("/api/v1/admin/problems", admin_auth(), body.dump(),
                           "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 201);

  auto j = json::parse(res->body);
  EXPECT_EQ(j["data"]["test_cases"].size(), 2u);

  created_problem_ids_.push_back(j["data"]["id"].get<int64_t>());
  for (auto& tc : j["data"]["test_cases"]) {
    created_testcase_ids_.push_back(tc["id"].get<int64_t>());
  }
}

TEST_F(AdminAPITest, CreateProblemDefaultValues) {
  json body = {{"title", "Minimal Problem"},
               {"description", "Just the essentials."}};
  auto res = client_->Post("/api/v1/admin/problems", admin_auth(), body.dump(),
                           "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 201);

  auto j = json::parse(res->body);
  EXPECT_EQ(j["data"]["difficulty"], "easy");  // default
  EXPECT_EQ(j["data"]["time_limit_ms"], 1000);
  EXPECT_EQ(j["data"]["memory_limit_kb"], 262144);

  created_problem_ids_.push_back(j["data"]["id"].get<int64_t>());
}

TEST_F(AdminAPITest, CreateProblemMissingTitle) {
  json body = {{"description", "No title here."}};
  auto res = client_->Post("/api/v1/admin/problems", admin_auth(), body.dump(),
                           "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 400);
}

TEST_F(AdminAPITest, CreateProblemMissingDescription) {
  json body = {{"title", "No Description"}};
  auto res = client_->Post("/api/v1/admin/problems", admin_auth(), body.dump(),
                           "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 400);
}

TEST_F(AdminAPITest, CreateProblemInvalidDifficulty) {
  json body = {{"title", "Bad Difficulty"},
               {"description", "Test."},
               {"difficulty", "impossible"}};
  auto res = client_->Post("/api/v1/admin/problems", admin_auth(), body.dump(),
                           "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 400);
}

TEST_F(AdminAPITest, CreateProblemInvalidJson) {
  auto res = client_->Post("/api/v1/admin/problems", admin_auth(),
                           "not valid json", "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 400);
}

// ═══════════════════════════════════════════════════════════════
// PUT /api/v1/admin/problems/:id — 编辑题目
// ═══════════════════════════════════════════════════════════════

TEST_F(AdminAPITest, UpdateProblemSuccess) {
  int64_t pid = create_problem_via_api("Original Title", "easy");
  ASSERT_GT(pid, 0);

  json body = {{"title", "Updated Title"},
               {"description", "Updated description."},
               {"difficulty", "hard"},
               {"time_limit_ms", 2000},
               {"memory_limit_kb", 131072}};
  auto res = client_->Put(
      "/api/v1/admin/problems/" + std::to_string(pid), admin_auth(),
      body.dump(), "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 200);

  auto j = json::parse(res->body);
  EXPECT_EQ(j["data"]["title"], "Updated Title");
  EXPECT_EQ(j["data"]["difficulty"], "hard");
  EXPECT_EQ(j["data"]["time_limit_ms"], 2000);
}

TEST_F(AdminAPITest, UpdateProblemNotFound) {
  json body = {{"title", "Ghost"},
               {"description", "Does not exist."},
               {"difficulty", "easy"}};
  auto res = client_->Put("/api/v1/admin/problems/99999", admin_auth(),
                          body.dump(), "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 404);
}

// ═══════════════════════════════════════════════════════════════
// DELETE /api/v1/admin/problems/:id — 删除题目
// ═══════════════════════════════════════════════════════════════

TEST_F(AdminAPITest, DeleteProblemSuccess) {
  int64_t pid = create_problem_via_api("To Be Deleted");
  ASSERT_GT(pid, 0);

  auto res = client_->Delete(
      "/api/v1/admin/problems/" + std::to_string(pid), admin_auth());
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 200);

  auto j = json::parse(res->body);
  EXPECT_EQ(j["data"]["deleted"], pid);

  // 确认已删除（DAO 验证）
  {
    auto guard = ConnectionPool::instance().acquire();
    ASSERT_TRUE(guard);
    auto p = ProblemDAO::FindById(*guard, pid);
    EXPECT_FALSE(p.has_value());
  }

  // 从清理列表移除
  created_problem_ids_.erase(
      std::remove(created_problem_ids_.begin(), created_problem_ids_.end(),
                  pid),
      created_problem_ids_.end());
}

TEST_F(AdminAPITest, DeleteProblemNotFound) {
  auto res = client_->Delete("/api/v1/admin/problems/99999", admin_auth());
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 404);
}

TEST_F(AdminAPITest, DeleteProblemCascadesTestCases) {
  int64_t pid = create_problem_via_api("Cascade Delete");
  ASSERT_GT(pid, 0);

  int64_t tcid = create_testcase_via_api(pid, "in", "out");
  ASSERT_GT(tcid, 0);

  auto res = client_->Delete(
      "/api/v1/admin/problems/" + std::to_string(pid), admin_auth());
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 200);

  // 确认测试用例也被删除
  {
    auto guard = ConnectionPool::instance().acquire();
    ASSERT_TRUE(guard);
    auto cases = TestCaseDAO::FindByProblemId(*guard, pid);
    EXPECT_EQ(static_cast<int>(cases.size()), 0);
  }

  created_problem_ids_.erase(
      std::remove(created_problem_ids_.begin(), created_problem_ids_.end(),
                  pid),
      created_problem_ids_.end());
  created_testcase_ids_.clear();
}

// ═══════════════════════════════════════════════════════════════
// POST /api/v1/admin/problems/:id/testcases — 添加测试用例
// ═══════════════════════════════════════════════════════════════

TEST_F(AdminAPITest, CreateTestCaseSuccess) {
  int64_t pid = create_problem_via_api("TestCase Problem");
  ASSERT_GT(pid, 0);

  json body = {{"input", "3\n1 2 3"},
               {"expected_output", "6"},
               {"is_sample", true},
               {"order_index", 1}};
  auto res = client_->Post(
      "/api/v1/admin/problems/" + std::to_string(pid) + "/testcases",
      admin_auth(), body.dump(), "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 201);

  auto j = json::parse(res->body);
  EXPECT_GT(j["data"]["id"].get<int64_t>(), 0);
  EXPECT_EQ(j["data"]["problem_id"], pid);
  EXPECT_EQ(j["data"]["input"], "3\n1 2 3");
  EXPECT_EQ(j["data"]["is_sample"], true);

  created_testcase_ids_.push_back(j["data"]["id"].get<int64_t>());
}

TEST_F(AdminAPITest, CreateTestCaseProblemNotFound) {
  json body = {{"input", "test"}, {"expected_output", "test"}};
  auto res = client_->Post("/api/v1/admin/problems/99999/testcases",
                           admin_auth(), body.dump(), "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 404);
}

TEST_F(AdminAPITest, CreateTestCaseMissingInput) {
  int64_t pid = create_problem_via_api("Missing Input");
  ASSERT_GT(pid, 0);

  json body = {{"expected_output", "output"}};
  auto res = client_->Post(
      "/api/v1/admin/problems/" + std::to_string(pid) + "/testcases",
      admin_auth(), body.dump(), "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 400);
}

TEST_F(AdminAPITest, CreateTestCaseMissingExpectedOutput) {
  int64_t pid = create_problem_via_api("Missing Expected");
  ASSERT_GT(pid, 0);

  json body = {{"input", "input only"}};
  auto res = client_->Post(
      "/api/v1/admin/problems/" + std::to_string(pid) + "/testcases",
      admin_auth(), body.dump(), "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 400);
}

// ═══════════════════════════════════════════════════════════════
// PUT /api/v1/admin/testcases/:id — 编辑测试用例
// ═══════════════════════════════════════════════════════════════

TEST_F(AdminAPITest, UpdateTestCaseSuccess) {
  int64_t pid = create_problem_via_api("Update TC");
  ASSERT_GT(pid, 0);

  int64_t tcid = create_testcase_via_api(pid, "old input", "old output", true,
                                         1);
  ASSERT_GT(tcid, 0);

  json body = {{"problem_id", pid},
               {"input", "new input"},
               {"expected_output", "new output"},
               {"is_sample", false},
               {"order_index", 2}};
  auto res = client_->Put(
      "/api/v1/admin/testcases/" + std::to_string(tcid), admin_auth(),
      body.dump(), "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 200);

  auto j = json::parse(res->body);
  EXPECT_EQ(j["data"]["input"], "new input");
  EXPECT_EQ(j["data"]["expected_output"], "new output");
  EXPECT_EQ(j["data"]["is_sample"], false);
  EXPECT_EQ(j["data"]["order_index"], 2);
}

TEST_F(AdminAPITest, UpdateTestCasePartial) {
  int64_t pid = create_problem_via_api("Partial Update TC");
  ASSERT_GT(pid, 0);

  int64_t tcid =
      create_testcase_via_api(pid, "original input", "original output");
  ASSERT_GT(tcid, 0);

  // 仅更新 order_index
  json body = {{"problem_id", pid}, {"order_index", 99}};
  auto res = client_->Put(
      "/api/v1/admin/testcases/" + std::to_string(tcid), admin_auth(),
      body.dump(), "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 200);

  auto j = json::parse(res->body);
  EXPECT_EQ(j["data"]["order_index"], 99);
  EXPECT_EQ(j["data"]["input"], "original input");  // 保持不变
}

TEST_F(AdminAPITest, UpdateTestCaseNotFound) {
  json body = {{"problem_id", 1}, {"input", "test"}};
  auto res = client_->Put("/api/v1/admin/testcases/99999", admin_auth(),
                          body.dump(), "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 404);
}

// ═══════════════════════════════════════════════════════════════
// DELETE /api/v1/admin/testcases/:id — 删除测试用例
// ═══════════════════════════════════════════════════════════════

TEST_F(AdminAPITest, DeleteTestCaseSuccess) {
  int64_t pid = create_problem_via_api("Delete TC");
  ASSERT_GT(pid, 0);

  int64_t tcid = create_testcase_via_api(pid);
  ASSERT_GT(tcid, 0);

  auto res = client_->Delete(
      "/api/v1/admin/testcases/" + std::to_string(tcid), admin_auth());
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 200);

  auto j = json::parse(res->body);
  EXPECT_EQ(j["data"]["deleted"], tcid);

  created_testcase_ids_.erase(
      std::remove(created_testcase_ids_.begin(), created_testcase_ids_.end(),
                  tcid),
      created_testcase_ids_.end());
}

TEST_F(AdminAPITest, DeleteTestCaseNotFound) {
  auto res = client_->Delete("/api/v1/admin/testcases/99999", admin_auth());
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 404);
}

// ═══════════════════════════════════════════════════════════════
// GET /api/v1/admin/users — 用户列表
// ═══════════════════════════════════════════════════════════════

TEST_F(AdminAPITest, ListUsersSuccess) {
  auto res = client_->Get("/api/v1/admin/users", admin_auth());
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 200);

  auto j = json::parse(res->body);
  EXPECT_TRUE(j["data"].contains("items"));
  EXPECT_TRUE(j["data"].contains("total"));
  EXPECT_GE(j["data"]["total"].get<int>(), 2);  // admin + normal user

  // 检查不包含 password_hash
  for (const auto& u : j["data"]["items"]) {
    EXPECT_FALSE(u.contains("password_hash"));
    EXPECT_TRUE(u.contains("username"));
    EXPECT_TRUE(u.contains("role"));
    EXPECT_TRUE(u.contains("status"));
  }
}

// ═══════════════════════════════════════════════════════════════
// PUT /api/v1/admin/users/:id — 修改用户角色/状态
// ═══════════════════════════════════════════════════════════════

TEST_F(AdminAPITest, UpdateUserRole) {
  json body = {{"role", "admin"}};
  auto res = client_->Put(
      "/api/v1/admin/users/" + std::to_string(user_id_), admin_auth(),
      body.dump(), "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 200);

  auto j = json::parse(res->body);
  EXPECT_EQ(j["data"]["role"], "admin");

  // 恢复角色
  json restore = {{"role", "user"}};
  client_->Put("/api/v1/admin/users/" + std::to_string(user_id_), admin_auth(),
               restore.dump(), "application/json");
}

TEST_F(AdminAPITest, UpdateUserStatus) {
  json body = {{"status", "disabled"}};
  auto res = client_->Put(
      "/api/v1/admin/users/" + std::to_string(user_id_), admin_auth(),
      body.dump(), "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 200);

  auto j = json::parse(res->body);
  EXPECT_EQ(j["data"]["status"], "disabled");

  // 恢复状态
  json restore = {{"status", "active"}};
  client_->Put("/api/v1/admin/users/" + std::to_string(user_id_), admin_auth(),
               restore.dump(), "application/json");
}

TEST_F(AdminAPITest, UpdateUserBothFields) {
  json body = {{"role", "admin"}, {"status", "disabled"}};
  auto res = client_->Put(
      "/api/v1/admin/users/" + std::to_string(user_id_), admin_auth(),
      body.dump(), "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 200);

  auto j = json::parse(res->body);
  EXPECT_EQ(j["data"]["role"], "admin");
  EXPECT_EQ(j["data"]["status"], "disabled");

  // 恢复
  json restore = {{"role", "user"}, {"status", "active"}};
  client_->Put("/api/v1/admin/users/" + std::to_string(user_id_), admin_auth(),
               restore.dump(), "application/json");
}

TEST_F(AdminAPITest, UpdateUserInvalidRole) {
  json body = {{"role", "superadmin"}};
  auto res = client_->Put(
      "/api/v1/admin/users/" + std::to_string(user_id_), admin_auth(),
      body.dump(), "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 400);
}

TEST_F(AdminAPITest, UpdateUserInvalidStatus) {
  json body = {{"status", "banned"}};
  auto res = client_->Put(
      "/api/v1/admin/users/" + std::to_string(user_id_), admin_auth(),
      body.dump(), "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 400);
}

TEST_F(AdminAPITest, UpdateUserNotFound) {
  json body = {{"role", "admin"}};
  auto res = client_->Put("/api/v1/admin/users/99999", admin_auth(),
                          body.dump(), "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 404);
}

TEST_F(AdminAPITest, UpdateUserNoFields) {
  json body = json::object();
  auto res = client_->Put(
      "/api/v1/admin/users/" + std::to_string(user_id_), admin_auth(),
      body.dump(), "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 400);
}

// ═══════════════════════════════════════════════════════════════
// GET /api/v1/admin/stats — 统计数据
// ═══════════════════════════════════════════════════════════════

TEST_F(AdminAPITest, StatsSuccess) {
  // 先创建一些题目和测试用例来生成数据
  int64_t pid = create_problem_via_api("Stats Problem", "medium");
  ASSERT_GT(pid, 0);

  auto res = client_->Get("/api/v1/admin/stats", admin_auth());
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 200);

  auto j = json::parse(res->body);
  EXPECT_TRUE(j["data"].contains("total_users"));
  EXPECT_TRUE(j["data"].contains("total_problems"));
  EXPECT_TRUE(j["data"].contains("total_submissions"));
  EXPECT_TRUE(j["data"].contains("submissions_by_status"));
  EXPECT_TRUE(j["data"].contains("daily_trend"));

  EXPECT_GE(j["data"]["total_users"].get<int>(), 2);
  EXPECT_GE(j["data"]["total_problems"].get<int>(), 1);

  // daily_trend 是数组
  EXPECT_TRUE(j["data"]["daily_trend"].is_array());
}

TEST_F(AdminAPITest, StatsRequiresAdmin) {
  auto res = client_->Get("/api/v1/admin/stats");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 401);
}

// ═══════════════════════════════════════════════════════════════
// 响应格式验证
// ═══════════════════════════════════════════════════════════════

TEST_F(AdminAPITest, ResponseEnvelopeFormat) {
  // 成功响应
  auto res = client_->Get("/api/v1/admin/users", admin_auth());
  ASSERT_TRUE(res != nullptr);
  auto j = json::parse(res->body);
  EXPECT_TRUE(j.contains("data"));
  EXPECT_EQ(j["message"], "ok");
  EXPECT_FALSE(j.contains("error"));
}

TEST_F(AdminAPITest, ErrorResponseFormat) {
  auto res = client_->Get("/api/v1/admin/users");  // 无认证
  ASSERT_TRUE(res != nullptr);
  auto j = json::parse(res->body);
  EXPECT_TRUE(j.contains("error"));
  EXPECT_FALSE(j.contains("data"));
}

// ═══════════════════════════════════════════════════════════════
// 综合流程测试
// ═══════════════════════════════════════════════════════════════

TEST_F(AdminAPITest, FullProblemLifecycle) {
  // 1. 创建题目
  json body = {{"title", "Lifecycle Test"},
               {"description", "Testing full CRUD lifecycle."},
               {"difficulty", "hard"},
               {"time_limit_ms", 5000},
               {"memory_limit_kb", 524288}};
  auto res = client_->Post("/api/v1/admin/problems", admin_auth(), body.dump(),
                           "application/json");
  ASSERT_TRUE(res != nullptr);
  ASSERT_EQ(res->status, 201);
  int64_t pid = json::parse(res->body)["data"]["id"].get<int64_t>();
  created_problem_ids_.push_back(pid);

  // 2. 添加测试用例
  json tc_body = {{"input", "5\n"}, {"expected_output", "5\n"},
                  {"is_sample", true}, {"order_index", 1}};
  res = client_->Post(
      "/api/v1/admin/problems/" + std::to_string(pid) + "/testcases",
      admin_auth(), tc_body.dump(), "application/json");
  ASSERT_TRUE(res != nullptr);
  ASSERT_EQ(res->status, 201);
  int64_t tcid = json::parse(res->body)["data"]["id"].get<int64_t>();
  created_testcase_ids_.push_back(tcid);

  // 3. 编辑题目
  json upd_body = {{"title", "Lifecycle Test Updated"},
                   {"description", "Updated description."},
                   {"difficulty", "medium"},
                   {"time_limit_ms", 3000},
                   {"memory_limit_kb", 262144}};
  res = client_->Put("/api/v1/admin/problems/" + std::to_string(pid),
                     admin_auth(), upd_body.dump(), "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 200);
  EXPECT_EQ(json::parse(res->body)["data"]["title"], "Lifecycle Test Updated");

  // 4. 编辑测试用例
  json tc_upd = {{"problem_id", pid},
                 {"input", "10\n"},
                 {"expected_output", "10\n"},
                 {"is_sample", false},
                 {"order_index", 2}};
  res = client_->Put("/api/v1/admin/testcases/" + std::to_string(tcid),
                     admin_auth(), tc_upd.dump(), "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 200);

  // 5. 删除测试用例
  res = client_->Delete("/api/v1/admin/testcases/" + std::to_string(tcid),
                        admin_auth());
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 200);
  created_testcase_ids_.clear();

  // 6. 删除题目
  res = client_->Delete("/api/v1/admin/problems/" + std::to_string(pid),
                        admin_auth());
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 200);
  created_problem_ids_.clear();
}

// ═══════════════════════════════════════════════════════════════
// 数据验证测试（通过 DAO 验证 API 写入的数据）
// ═══════════════════════════════════════════════════════════════

TEST_F(AdminAPITest, CreatedProblemPersistedCorrectly) {
  json body = {{"title", "Persist Test"},
               {"description", "Verify persistence."},
               {"difficulty", "medium"},
               {"time_limit_ms", 3000},
               {"memory_limit_kb", 131072}};
  auto res = client_->Post("/api/v1/admin/problems", admin_auth(), body.dump(),
                           "application/json");
  ASSERT_TRUE(res != nullptr);
  ASSERT_EQ(res->status, 201);
  int64_t pid = json::parse(res->body)["data"]["id"].get<int64_t>();
  created_problem_ids_.push_back(pid);

  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);
  auto p = ProblemDAO::FindById(*guard, pid);
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->title, "Persist Test");
  EXPECT_EQ(p->difficulty, Difficulty::medium);
  EXPECT_EQ(p->time_limit_ms, 3000);
  EXPECT_EQ(p->memory_limit_kb, 131072);
}

TEST_F(AdminAPITest, CreatedTestCasePersistedCorrectly) {
  int64_t pid = create_problem_via_api("TC Persist");
  ASSERT_GT(pid, 0);

  json tc_body = {{"input", "42\n"},
                  {"expected_output", "42\n"},
                  {"is_sample", true},
                  {"order_index", 5}};
  auto res = client_->Post(
      "/api/v1/admin/problems/" + std::to_string(pid) + "/testcases",
      admin_auth(), tc_body.dump(), "application/json");
  ASSERT_TRUE(res != nullptr);
  ASSERT_EQ(res->status, 201);
  int64_t tcid = json::parse(res->body)["data"]["id"].get<int64_t>();
  created_testcase_ids_.push_back(tcid);

  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);
  auto cases = TestCaseDAO::FindByProblemId(*guard, pid);
  ASSERT_GE(static_cast<int>(cases.size()), 1);

  bool found = false;
  for (const auto& tc : cases) {
    if (tc.id == tcid) {
      EXPECT_EQ(tc.input, "42\n");
      EXPECT_EQ(tc.expected_output, "42\n");
      EXPECT_TRUE(tc.is_sample);
      EXPECT_EQ(tc.order_index, 5);
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

// ═══════════════════════════════════════════════════════════════
// 边界条件测试
// ═══════════════════════════════════════════════════════════════

TEST_F(AdminAPITest, CreateProblemWithLongTitle) {
  std::string long_title(200, 'T');
  json body = {{"title", long_title}, {"description", "Long title test."}};
  auto res = client_->Post("/api/v1/admin/problems", admin_auth(), body.dump(),
                           "application/json");
  ASSERT_TRUE(res != nullptr);
  // 应成功或按 DB 限制报错
  if (res->status == 201) {
    int64_t pid = json::parse(res->body)["data"]["id"].get<int64_t>();
    created_problem_ids_.push_back(pid);
  }
}

TEST_F(AdminAPITest, CreateProblemWithSpecialCharacters) {
  json body = {{"title", "Problem with \"quotes\" & <tags>"},
               {"description", "Description with 'single' and \"double\" quotes and\nnewlines."}};
  auto res = client_->Post("/api/v1/admin/problems", admin_auth(), body.dump(),
                           "application/json");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 201);
  int64_t pid = json::parse(res->body)["data"]["id"].get<int64_t>();
  created_problem_ids_.push_back(pid);

  // 验证原始内容是否正确保存
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);
  auto p = ProblemDAO::FindById(*guard, pid);
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->title, "Problem with \"quotes\" & <tags>");
}

TEST_F(AdminAPITest, MultipleTestCasesOrdering) {
  int64_t pid = create_problem_via_api("Ordering Test");
  ASSERT_GT(pid, 0);

  // 逆序添加测试用例
  create_testcase_via_api(pid, "third", "3", false, 3);
  create_testcase_via_api(pid, "first", "1", true, 1);
  create_testcase_via_api(pid, "second", "2", false, 2);

  // 验证按 order_index 排序
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);
  auto cases = TestCaseDAO::FindByProblemId(*guard, pid);
  ASSERT_EQ(static_cast<int>(cases.size()), 3);
  EXPECT_EQ(cases[0].order_index, 1);
  EXPECT_EQ(cases[1].order_index, 2);
  EXPECT_EQ(cases[2].order_index, 3);
}

}  // namespace
}  // namespace vibeoj
