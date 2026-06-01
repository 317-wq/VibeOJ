// 管理后台处理器 — 题目CRUD + 测试用例管理 + 用户管理 + 统计数据。
#include "handler/admin_handler.h"

#include "auth/middleware.h"
#include "common/log.h"
#include "db/connection_pool.h"
#include "db/dao.h"
#include "httplib.h"
#include "model/problem.h"
#include "model/user.h"
#include "nlohmann/json.hpp"

#include <cppconn/connection.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>

namespace vibeoj {

using json = nlohmann::json;

namespace {

// ── helper: JSON 错误响应 ──────────────────────────────────────
void send_error(httplib::Response& res, int status, const std::string& msg) {
  res.status = status;
  res.set_content(json{{"error", msg}}.dump(), "application/json");
}

// ── helper: JSON 成功响应 ──────────────────────────────────────
void send_ok(httplib::Response& res, const json& data, int status = 200) {
  res.status = status;
  res.set_content(json{{"data", data}, {"message", "ok"}}.dump(),
                  "application/json");
}

// ── 认证 + 管理员权限检查；失败时自动发送错误响应 ───────────────
bool authenticate_admin(const httplib::Request& req, httplib::Response& res,
                        const std::string& secret, User& out_user) {
  std::string auth_header =
      req.has_header("Authorization") ? req.get_header_value("Authorization")
                                       : "";
  if (!authenticate_request(auth_header, secret, out_user)) {
    send_error(res, 401, "authentication required");
    return false;
  }
  if (!require_admin(out_user)) {
    send_error(res, 403, "admin privileges required");
    return false;
  }
  return true;
}

// ── Problem → JSON（管理后台用，含 created_by）─────────────────
json problem_to_admin_json(const Problem& p) {
  return {{"id", p.id},
          {"title", p.title},
          {"description", p.description},
          {"difficulty", difficulty_to_string(p.difficulty)},
          {"time_limit_ms", p.time_limit_ms},
          {"memory_limit_kb", p.memory_limit_kb},
          {"created_by", p.created_by},
          {"created_at", p.created_at}};
}

// ── TestCase → JSON ────────────────────────────────────────────
json testcase_to_json(const TestCase& tc) {
  return {{"id", tc.id},
          {"problem_id", tc.problem_id},
          {"input", tc.input},
          {"expected_output", tc.expected_output},
          {"is_sample", tc.is_sample},
          {"order_index", tc.order_index}};
}

// ── User → JSON（不含 password_hash）───────────────────────────
json user_to_admin_json(const User& u) {
  return {{"id", u.id},
          {"username", u.username},
          {"role", user_role_to_string(u.role)},
          {"status", user_status_to_string(u.status)},
          {"created_at", u.created_at}};
}

// ── 解析并验证题目请求体 ───────────────────────────────────────
bool parse_problem_body(const httplib::Request& req, httplib::Response& res,
                        std::string& title, std::string& description,
                        Difficulty& diff, int& time_limit_ms,
                        int& memory_limit_kb) {
  json body;
  try {
    body = json::parse(req.body);
  } catch (...) {
    send_error(res, 400, "invalid JSON body");
    return false;
  }

  if (!body.contains("title") || !body["title"].is_string() ||
      body["title"].get<std::string>().empty()) {
    send_error(res, 400, "title is required");
    return false;
  }
  if (!body.contains("description") || !body["description"].is_string()) {
    send_error(res, 400, "description is required");
    return false;
  }

  title = body["title"].get<std::string>();
  description = body["description"].get<std::string>();

  // 难度（默认 easy）
  diff = Difficulty::easy;
  if (body.contains("difficulty") && body["difficulty"].is_string()) {
    std::string d = body["difficulty"].get<std::string>();
    if (d != "easy" && d != "medium" && d != "hard") {
      send_error(res, 400, "invalid difficulty: must be easy/medium/hard");
      return false;
    }
    diff = difficulty_from_string(d);
  }

  time_limit_ms = 1000;
  if (body.contains("time_limit_ms")) {
    if (!body["time_limit_ms"].is_number()) {
      send_error(res, 400, "time_limit_ms must be a number");
      return false;
    }
    time_limit_ms = body["time_limit_ms"].get<int>();
  }

  memory_limit_kb = 262144;
  if (body.contains("memory_limit_kb")) {
    if (!body["memory_limit_kb"].is_number()) {
      send_error(res, 400, "memory_limit_kb must be a number");
      return false;
    }
    memory_limit_kb = body["memory_limit_kb"].get<int>();
  }

  return true;
}

}  // namespace

void AdminHandler::register_routes(httplib::Server& server,
                                    const ServerConfig& cfg) {
  // 拷贝 secret 字符串，避免 lambda 捕获引用在 cfg 析构后悬空。
  std::string secret = cfg.jwt_secret;

  // ═══════════════════════════════════════════════════════════════
  // POST /api/v1/admin/problems — 创建题目（含可选测试用例）
  // ═══════════════════════════════════════════════════════════════
  server.Post("/api/v1/admin/problems",
              [secret](const httplib::Request& req, httplib::Response& res) {
    User admin;
    if (!authenticate_admin(req, res, secret, admin)) return;

    std::string title, description;
    Difficulty diff;
    int time_limit_ms, memory_limit_kb;
    if (!parse_problem_body(req, res, title, description, diff, time_limit_ms,
                            memory_limit_kb))
      return;

    auto guard = ConnectionPool::instance().acquire();
    if (!guard) {
      LOG_ERROR("AdminHandler::create_problem: DB unavailable");
      return send_error(res, 503, "database unavailable");
    }

    Problem p;
    p.title = title;
    p.description = description;
    p.difficulty = diff;
    p.time_limit_ms = time_limit_ms;
    p.memory_limit_kb = memory_limit_kb;
    p.created_by = admin.id;

    int64_t pid = ProblemDAO::Create(*guard, p);
    if (pid <= 0) {
      LOG_ERROR("AdminHandler::create_problem: insert failed");
      return send_error(res, 500, "failed to create problem");
    }

    // 解析并创建可选的测试用例
    json body;
    try { body = json::parse(req.body); } catch (...) { body = {}; }

    json created_cases = json::array();
    if (body.contains("test_cases") && body["test_cases"].is_array()) {
      for (const auto& tc_json : body["test_cases"]) {
        TestCase tc;
        tc.problem_id = pid;
        tc.input = tc_json.value("input", "");
        tc.expected_output = tc_json.value("expected_output", "");
        tc.is_sample = tc_json.value("is_sample", false);
        tc.order_index = tc_json.value("order_index", 0);

        int64_t tcid = TestCaseDAO::Create(*guard, tc);
        if (tcid > 0) {
          created_cases.push_back(testcase_to_json(tc));
        }
      }
    }

    auto created = ProblemDAO::FindById(*guard, pid);
    json result = created.has_value() ? problem_to_admin_json(*created)
                                       : json{{"id", pid}};
    result["test_cases"] = created_cases;

    LOG_INFO("Admin %ld created problem %ld: %s", admin.id, pid, title.c_str());
    send_ok(res, result, 201);
  });

  // ═══════════════════════════════════════════════════════════════
  // PUT /api/v1/admin/problems/:id — 编辑题目
  // ═══════════════════════════════════════════════════════════════
  server.Put(R"(/api/v1/admin/problems/(\d+))",
             [secret](const httplib::Request& req, httplib::Response& res) {
    User admin;
    if (!authenticate_admin(req, res, secret, admin)) return;

    int64_t problem_id = std::stoll(req.matches[1]);

    std::string title, description;
    Difficulty diff;
    int time_limit_ms, memory_limit_kb;
    if (!parse_problem_body(req, res, title, description, diff, time_limit_ms,
                            memory_limit_kb))
      return;

    auto guard = ConnectionPool::instance().acquire();
    if (!guard) {
      LOG_ERROR("AdminHandler::update_problem: DB unavailable");
      return send_error(res, 503, "database unavailable");
    }

    auto existing = ProblemDAO::FindById(*guard, problem_id);
    if (!existing.has_value()) {
      return send_error(res, 404, "problem not found");
    }

    Problem p;
    p.id = problem_id;
    p.title = title;
    p.description = description;
    p.difficulty = diff;
    p.time_limit_ms = time_limit_ms;
    p.memory_limit_kb = memory_limit_kb;

    if (!ProblemDAO::Update(*guard, p)) {
      LOG_ERROR("AdminHandler::update_problem: update failed for %ld",
                problem_id);
      return send_error(res, 500, "failed to update problem");
    }

    auto updated = ProblemDAO::FindById(*guard, problem_id);
    LOG_INFO("Admin %ld updated problem %ld", admin.id, problem_id);
    send_ok(res, updated.has_value() ? problem_to_admin_json(*updated)
                                      : json{{"id", problem_id}});
  });

  // ═══════════════════════════════════════════════════════════════
  // DELETE /api/v1/admin/problems/:id — 删除题目（级联删除测试用例）
  // ═══════════════════════════════════════════════════════════════
  server.Delete(R"(/api/v1/admin/problems/(\d+))",
                [secret](const httplib::Request& req, httplib::Response& res) {
    User admin;
    if (!authenticate_admin(req, res, secret, admin)) return;

    int64_t problem_id = std::stoll(req.matches[1]);

    auto guard = ConnectionPool::instance().acquire();
    if (!guard) {
      LOG_ERROR("AdminHandler::delete_problem: DB unavailable");
      return send_error(res, 503, "database unavailable");
    }

    auto existing = ProblemDAO::FindById(*guard, problem_id);
    if (!existing.has_value()) {
      return send_error(res, 404, "problem not found");
    }

    // 先删测试用例
    TestCaseDAO::DeleteByProblemId(*guard, problem_id);
    // 再删题目
    if (!ProblemDAO::Delete(*guard, problem_id)) {
      return send_error(res, 500, "failed to delete problem");
    }

    LOG_INFO("Admin %ld deleted problem %ld", admin.id, problem_id);
    send_ok(res, {{"deleted", problem_id}});
  });

  // ═══════════════════════════════════════════════════════════════
  // POST /api/v1/admin/problems/:id/testcases — 添加测试用例
  // ═══════════════════════════════════════════════════════════════
  server.Post(R"(/api/v1/admin/problems/(\d+)/testcases)",
              [secret](const httplib::Request& req, httplib::Response& res) {
    User admin;
    if (!authenticate_admin(req, res, secret, admin)) return;

    int64_t problem_id = std::stoll(req.matches[1]);

    json body;
    try {
      body = json::parse(req.body);
    } catch (...) {
      return send_error(res, 400, "invalid JSON body");
    }

    if (!body.contains("input") || !body["input"].is_string()) {
      return send_error(res, 400, "input is required");
    }
    if (!body.contains("expected_output") ||
        !body["expected_output"].is_string()) {
      return send_error(res, 400, "expected_output is required");
    }

    auto guard = ConnectionPool::instance().acquire();
    if (!guard) {
      LOG_ERROR("AdminHandler::create_testcase: DB unavailable");
      return send_error(res, 503, "database unavailable");
    }

    // 验证题目存在
    auto problem = ProblemDAO::FindById(*guard, problem_id);
    if (!problem.has_value()) {
      return send_error(res, 404, "problem not found");
    }

    TestCase tc;
    tc.problem_id = problem_id;
    tc.input = body["input"].get<std::string>();
    tc.expected_output = body["expected_output"].get<std::string>();
    tc.is_sample = body.value("is_sample", false);
    tc.order_index = body.value("order_index", 0);

    int64_t tcid = TestCaseDAO::Create(*guard, tc);
    if (tcid <= 0) {
      LOG_ERROR("AdminHandler::create_testcase: insert failed");
      return send_error(res, 500, "failed to create test case");
    }

    LOG_INFO("Admin %ld created testcase %ld for problem %ld", admin.id, tcid,
             problem_id);
    send_ok(res, testcase_to_json(tc), 201);
  });

  // ═══════════════════════════════════════════════════════════════
  // PUT /api/v1/admin/testcases/:id — 编辑测试用例
  // ═══════════════════════════════════════════════════════════════
  server.Put(R"(/api/v1/admin/testcases/(\d+))",
             [secret](const httplib::Request& req, httplib::Response& res) {
    User admin;
    if (!authenticate_admin(req, res, secret, admin)) return;

    int64_t tc_id = std::stoll(req.matches[1]);

    json body;
    try {
      body = json::parse(req.body);
    } catch (...) {
      return send_error(res, 400, "invalid JSON body");
    }

    if (!body.contains("problem_id") || !body["problem_id"].is_number()) {
      return send_error(res, 400, "problem_id is required");
    }

    auto guard = ConnectionPool::instance().acquire();
    if (!guard) {
      LOG_ERROR("AdminHandler::update_testcase: DB unavailable");
      return send_error(res, 503, "database unavailable");
    }

    // 验证测试用例存在
    auto all_cases =
        TestCaseDAO::FindByProblemId(*guard, body["problem_id"].get<int64_t>());
    bool found = false;
    TestCase existing;
    for (const auto& c : all_cases) {
      if (c.id == tc_id) {
        existing = c;
        found = true;
        break;
      }
    }
    if (!found) {
      return send_error(res, 404, "test case not found");
    }

    TestCase tc;
    tc.id = tc_id;
    tc.problem_id = body["problem_id"].get<int64_t>();
    tc.input = body.value("input", existing.input);
    tc.expected_output =
        body.value("expected_output", existing.expected_output);
    tc.is_sample = body.value("is_sample", existing.is_sample);
    tc.order_index = body.value("order_index", existing.order_index);

    if (!TestCaseDAO::Update(*guard, tc)) {
      LOG_ERROR("AdminHandler::update_testcase: update failed for %ld", tc_id);
      return send_error(res, 500, "failed to update test case");
    }

    LOG_INFO("Admin %ld updated testcase %ld", admin.id, tc_id);
    send_ok(res, testcase_to_json(tc));
  });

  // ═══════════════════════════════════════════════════════════════
  // DELETE /api/v1/admin/testcases/:id — 删除测试用例
  // ═══════════════════════════════════════════════════════════════
  server.Delete(R"(/api/v1/admin/testcases/(\d+))",
                [secret](const httplib::Request& req, httplib::Response& res) {
    User admin;
    if (!authenticate_admin(req, res, secret, admin)) return;

    int64_t tc_id = std::stoll(req.matches[1]);

    auto guard = ConnectionPool::instance().acquire();
    if (!guard) {
      LOG_ERROR("AdminHandler::delete_testcase: DB unavailable");
      return send_error(res, 503, "database unavailable");
    }

    if (!TestCaseDAO::Delete(*guard, tc_id)) {
      return send_error(res, 404, "test case not found");
    }

    LOG_INFO("Admin %ld deleted testcase %ld", admin.id, tc_id);
    send_ok(res, {{"deleted", tc_id}});
  });

  // ═══════════════════════════════════════════════════════════════
  // GET /api/v1/admin/users — 用户列表
  // ═══════════════════════════════════════════════════════════════
  server.Get("/api/v1/admin/users",
             [secret](const httplib::Request& req, httplib::Response& res) {
    User admin;
    if (!authenticate_admin(req, res, secret, admin)) return;

    auto guard = ConnectionPool::instance().acquire();
    if (!guard) {
      LOG_ERROR("AdminHandler::list_users: DB unavailable");
      return send_error(res, 503, "database unavailable");
    }

    auto users = UserDAO::List(*guard);

    json items = json::array();
    for (const auto& u : users) {
      items.push_back(user_to_admin_json(u));
    }

    send_ok(res, {{"items", items}, {"total", users.size()}});
  });

  // ═══════════════════════════════════════════════════════════════
  // PUT /api/v1/admin/users/:id — 修改用户角色/状态
  // ═══════════════════════════════════════════════════════════════
  server.Put(R"(/api/v1/admin/users/(\d+))",
             [secret](const httplib::Request& req, httplib::Response& res) {
    User admin;
    if (!authenticate_admin(req, res, secret, admin)) return;

    int64_t user_id = std::stoll(req.matches[1]);

    json body;
    try {
      body = json::parse(req.body);
    } catch (...) {
      return send_error(res, 400, "invalid JSON body");
    }

    auto guard = ConnectionPool::instance().acquire();
    if (!guard) {
      LOG_ERROR("AdminHandler::update_user: DB unavailable");
      return send_error(res, 503, "database unavailable");
    }

    auto existing = UserDAO::FindById(*guard, user_id);
    if (!existing.has_value()) {
      return send_error(res, 404, "user not found");
    }

    bool changed = false;

    // 更新角色
    if (body.contains("role") && body["role"].is_string()) {
      std::string r = body["role"].get<std::string>();
      if (r != "admin" && r != "user") {
        return send_error(res, 400, "invalid role: must be admin or user");
      }
      if (UserDAO::UpdateRole(*guard, user_id, user_role_from_string(r))) {
        changed = true;
      }
    }

    // 更新状态
    if (body.contains("status") && body["status"].is_string()) {
      std::string s = body["status"].get<std::string>();
      if (s != "active" && s != "disabled") {
        return send_error(res, 400,
                           "invalid status: must be active or disabled");
      }
      if (UserDAO::UpdateStatus(*guard, user_id, user_status_from_string(s))) {
        changed = true;
      }
    }

    if (!changed && !body.contains("role") && !body.contains("status")) {
      return send_error(res, 400, "role or status is required");
    }

    auto updated = UserDAO::FindById(*guard, user_id);
    LOG_INFO("Admin %ld updated user %ld", admin.id, user_id);
    send_ok(res, updated.has_value() ? user_to_admin_json(*updated)
                                      : json{{"id", user_id}});
  });

  // ═══════════════════════════════════════════════════════════════
  // GET /api/v1/admin/stats — 统计数据（用户数/题目数/提交量/通过率）
  // ═══════════════════════════════════════════════════════════════
  server.Get("/api/v1/admin/stats",
             [secret](const httplib::Request& req, httplib::Response& res) {
    User admin;
    if (!authenticate_admin(req, res, secret, admin)) return;

    auto guard = ConnectionPool::instance().acquire();
    if (!guard) {
      LOG_ERROR("AdminHandler::stats: DB unavailable");
      return send_error(res, 503, "database unavailable");
    }

    json stats;

    try {
      auto& conn = *guard;
      auto stmt = conn.createStatement();

      // 用户总数
      {
        auto rs = stmt->executeQuery("SELECT COUNT(*) FROM users");
        stats["total_users"] = rs->next() ? rs->getInt64(1) : 0;
        delete rs;
      }

      // 题目总数
      {
        auto rs = stmt->executeQuery("SELECT COUNT(*) FROM problems");
        stats["total_problems"] = rs->next() ? rs->getInt64(1) : 0;
        delete rs;
      }

      // 提交总数
      {
        auto rs = stmt->executeQuery("SELECT COUNT(*) FROM submissions");
        stats["total_submissions"] = rs->next() ? rs->getInt64(1) : 0;
        delete rs;
      }

      // 按状态统计提交数
      {
        json by_status = json::object();
        auto rs = stmt->executeQuery(
            "SELECT status, COUNT(*) as cnt FROM submissions "
            "GROUP BY status");
        while (rs->next()) {
          by_status[rs->getString("status").asStdString()] =
              rs->getInt64("cnt");
        }
        delete rs;
        stats["submissions_by_status"] = by_status;
      }

      // 最近 7 天每日提交趋势
      {
        json daily = json::array();
        auto rs = stmt->executeQuery(
            "SELECT DATE_FORMAT(created_at, '%Y-%m-%d') as date, "
            "COUNT(*) as cnt FROM submissions "
            "WHERE created_at >= DATE_SUB(NOW(), INTERVAL 7 DAY) "
            "GROUP BY DATE_FORMAT(created_at, '%Y-%m-%d') "
            "ORDER BY date");
        while (rs->next()) {
          daily.push_back({{"date", rs->getString("date").asStdString()},
                           {"count", rs->getInt64("cnt")}});
        }
        delete rs;
        stats["daily_trend"] = daily;
      }

      delete stmt;
    } catch (const sql::SQLException& e) {
      LOG_ERROR("AdminHandler::stats: SQL error: %s", e.what());
      return send_error(res, 500, "failed to query statistics");
    }

    send_ok(res, stats);
  });

  LOG_INFO("AdminHandler routes registered");
}

}  // namespace vibeoj
