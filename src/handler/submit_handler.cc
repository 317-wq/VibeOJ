// 提交处理器 — 代码提交、判题结果查询、提交列表。
#include "handler/submit_handler.h"

#include "auth/middleware.h"
#include "common/log.h"
#include "db/connection_pool.h"
#include "db/dao.h"
#include "httplib.h"
#include "model/submission.h"
#include "model/user.h"
#include "nlohmann/json.hpp"

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
  res.set_content(json{
      {"data", data},
      {"message", "ok"}
  }.dump(), "application/json");
}

// ── Submission → JSON ──────────────────────────────────────────
json submission_to_json(const Submission& s) {
  return {
      {"id", s.id},
      {"user_id", s.user_id},
      {"problem_id", s.problem_id},
      {"code", s.code},
      {"status", submission_status_to_string(s.status)},
      {"compile_output", s.compile_output},
      {"passed_cases", s.passed_cases},
      {"total_cases", s.total_cases},
      {"time_used_ms", s.time_used_ms},
      {"memory_used_kb", s.memory_used_kb},
      {"diff_output", s.diff_output},
      {"created_at", s.created_at},
      {"updated_at", s.updated_at}
  };
}

// ── 从请求中提取并验证用户身份；失败时自动发送 401 响应 ───────
bool authenticate(const httplib::Request& req, httplib::Response& res,
                  const std::string& secret, User& out_user) {
  std::string auth_header = req.has_header("Authorization")
      ? req.get_header_value("Authorization") : "";
  if (!authenticate_request(auth_header, secret, out_user)) {
    send_error(res, 401, "authentication required");
    return false;
  }
  return true;
}

// ── 解析分页参数 ───────────────────────────────────────────────
void parse_pagination(const httplib::Request& req, int& page, int& page_size) {
  page = 1;
  page_size = 20;
  if (req.has_param("page")) {
    try {
      page = std::stoi(req.get_param_value("page"));
      if (page < 1) page = 1;
    } catch (...) { page = 1; }
  }
  if (req.has_param("page_size")) {
    try {
      page_size = std::stoi(req.get_param_value("page_size"));
      if (page_size < 1) page_size = 1;
      if (page_size > 100) page_size = 100;
    } catch (...) { page_size = 20; }
  }
}

}  // namespace

void SubmitHandler::register_routes(httplib::Server& server,
                                    const ServerConfig& cfg) {
  const std::string& secret = cfg.jwt_secret;

  // ── POST /api/v1/submissions ─────────────────────────────────
  // 提交代码：接收 {problem_id, code}，写入 submission 并返回 id。
  server.Post("/api/v1/submissions", [&secret](const httplib::Request& req,
                                                httplib::Response& res) {
    // 1. 认证
    User user;
    if (!authenticate(req, res, secret, user)) return;

    // 2. 解析请求体
    json body;
    try {
      body = json::parse(req.body);
    } catch (...) {
      return send_error(res, 400, "invalid JSON body");
    }

    if (!body.contains("problem_id") || !body["problem_id"].is_number()) {
      return send_error(res, 400, "problem_id is required and must be a number");
    }
    if (!body.contains("code") || !body["code"].is_string() ||
        body["code"].get<std::string>().empty()) {
      return send_error(res, 400, "code is required and must not be empty");
    }

    int64_t problem_id = body["problem_id"].get<int64_t>();
    std::string code = body["code"].get<std::string>();

    // 3. 验证题目存在
    auto guard = ConnectionPool::instance().acquire();
    if (!guard) {
      LOG_ERROR("SubmitHandler::create: failed to acquire DB connection");
      return send_error(res, 503, "database unavailable");
    }

    auto problem = ProblemDAO::FindById(*guard, problem_id);
    if (!problem.has_value()) {
      return send_error(res, 404, "problem not found");
    }

    // 4. 创建 submission（默认状态 pending）
    Submission sub;
    sub.user_id = user.id;
    sub.problem_id = problem_id;
    sub.code = code;
    sub.status = SubmissionStatus::pending;

    int64_t sub_id = SubmissionDAO::Create(*guard, sub);
    if (sub_id <= 0) {
      LOG_ERROR("SubmitHandler::create: failed to create submission");
      return send_error(res, 500, "failed to create submission");
    }

    LOG_INFO("Submission %ld created: user=%ld problem=%ld",
             sub_id, user.id, problem_id);

    // 5. 返回 submission_id（后续判题线程池会从 pending 队列取任务）
    send_ok(res, {{"submission_id", sub_id}}, 201);
  });

  // ── GET /api/v1/submissions/:id ──────────────────────────────
  // 查询单条提交详情（用户只能查看自己的提交，admin 可查看所有）。
  server.Get(R"(/api/v1/submissions/(\d+))", [&secret](const httplib::Request& req,
                                                         httplib::Response& res) {
    User user;
    if (!authenticate(req, res, secret, user)) return;

    int64_t sub_id = std::stoll(req.matches[1]);

    auto guard = ConnectionPool::instance().acquire();
    if (!guard) {
      LOG_ERROR("SubmitHandler::detail: failed to acquire DB connection");
      return send_error(res, 503, "database unavailable");
    }

    auto sub = SubmissionDAO::FindById(*guard, sub_id);
    if (!sub.has_value()) {
      return send_error(res, 404, "submission not found");
    }

    // 非 admin 用户只能查看自己的提交
    if (user.role != UserRole::admin && sub->user_id != user.id) {
      return send_error(res, 403, "access denied");
    }

    send_ok(res, submission_to_json(*sub));
  });

  // ── GET /api/v1/submissions ──────────────────────────────────
  // 提交列表，支持 ?problem_id=&user_id=&page=&page_size=。
  // 普通用户只能查看自己的提交；admin 可通过 user_id 查看他人。
  server.Get("/api/v1/submissions", [&secret](const httplib::Request& req,
                                               httplib::Response& res) {
    User user;
    if (!authenticate(req, res, secret, user)) return;

    // 解析筛选参数
    int64_t filter_user_id = 0;   // 0 = 不过滤
    int64_t filter_problem_id = 0;

    if (req.has_param("user_id")) {
      try {
        filter_user_id = std::stoll(req.get_param_value("user_id"));
      } catch (...) {
        return send_error(res, 400, "invalid user_id");
      }
    }
    if (req.has_param("problem_id")) {
      try {
        filter_problem_id = std::stoll(req.get_param_value("problem_id"));
      } catch (...) {
        return send_error(res, 400, "invalid problem_id");
      }
    }

    // 非 admin 用户：只能查看自己的提交（忽略 user_id 参数或强制设为自身）
    if (user.role != UserRole::admin) {
      filter_user_id = user.id;
    }
    // admin 未指定 user_id 时查看全部

    int page, page_size;
    parse_pagination(req, page, page_size);

    auto guard = ConnectionPool::instance().acquire();
    if (!guard) {
      LOG_ERROR("SubmitHandler::list: failed to acquire DB connection");
      return send_error(res, 503, "database unavailable");
    }

    auto result = SubmissionDAO::List(*guard, filter_user_id,
                                      filter_problem_id, page, page_size);

    json items = json::array();
    for (const auto& s : result.submissions) {
      items.push_back(submission_to_json(s));
    }

    send_ok(res, {
        {"items", items},
        {"total", result.total},
        {"page", page},
        {"page_size", page_size}
    });
  });

  LOG_INFO("SubmitHandler routes registered");
}

}  // namespace vibeoj
