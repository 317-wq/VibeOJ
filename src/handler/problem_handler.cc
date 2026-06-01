// 题目处理器 — 题目列表 (GET /problems) 与题目详情 (GET /problems/:id)。
#include "handler/problem_handler.h"

#include "common/log.h"
#include "db/connection_pool.h"
#include "db/dao.h"
#include "httplib.h"
#include "model/problem.h"
#include "nlohmann/json.hpp"

namespace vibeoj {

using json = nlohmann::json;

namespace {

// ── helper: JSON 错误响应 ──────────────────────────────────────
void send_error(httplib::Response& res, int status, const std::string& msg) {
  res.status = status;
  res.set_content(json{{"error", msg}}.dump(), "application/json");
}

// ── helper: JSON 成功响应 (200/201) ─────────────────────────────
void send_ok(httplib::Response& res, const json& data, int status = 200) {
  res.status = status;
  res.set_content(json{
      {"data", data},
      {"message", "ok"}
  }.dump(), "application/json");
}

// ── Problem → JSON (列表摘要，不含 description 与 test_cases) ──
json problem_to_summary_json(const Problem& p) {
  return {
      {"id", p.id},
      {"title", p.title},
      {"difficulty", difficulty_to_string(p.difficulty)},
      {"created_at", p.created_at}
  };
}

// ── Problem → JSON (完整详情，含 description + 样例 test_cases) ─
json problem_to_detail_json(const Problem& p,
                             const std::vector<TestCase>& sample_cases) {
  json j = {
      {"id", p.id},
      {"title", p.title},
      {"description", p.description},
      {"difficulty", difficulty_to_string(p.difficulty)},
      {"time_limit_ms", p.time_limit_ms},
      {"memory_limit_kb", p.memory_limit_kb},
      {"created_at", p.created_at}
  };
  // 仅返回 is_sample=true 的测试用例
  json samples = json::array();
  for (const auto& tc : sample_cases) {
    if (tc.is_sample) {
      samples.push_back({
          {"id", tc.id},
          {"input", tc.input},
          {"expected_output", tc.expected_output},
          {"order_index", tc.order_index}
      });
    }
  }
  j["sample_cases"] = samples;
  return j;
}

// ── 解析并规范化分页参数 ───────────────────────────────────────
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
      if (page_size > 100) page_size = 100;  // 上限 100
    } catch (...) { page_size = 20; }
  }
}

}  // namespace

void ProblemHandler::register_routes(httplib::Server& server) {
  // ── GET /api/v1/problems ─────────────────────────────────────
  // 题目列表，支持 ?difficulty=&page=&page_size=
  server.Get("/api/v1/problems", [](const httplib::Request& req,
                                     httplib::Response& res) {
    // 难度筛选
    std::string difficulty;
    if (req.has_param("difficulty")) {
      difficulty = req.get_param_value("difficulty");
      // 校验难度值
      if (!difficulty.empty() &&
          difficulty != "easy" && difficulty != "medium" && difficulty != "hard") {
        return send_error(res, 400, "invalid difficulty: must be easy/medium/hard");
      }
    }

    // 分页
    int page, page_size;
    parse_pagination(req, page, page_size);

    auto guard = ConnectionPool::instance().acquire();
    if (!guard) {
      LOG_ERROR("ProblemHandler::list: failed to acquire DB connection");
      return send_error(res, 503, "database unavailable");
    }

    auto result = ProblemDAO::FindAll(*guard, difficulty, page, page_size);

    json items = json::array();
    for (const auto& p : result.problems) {
      items.push_back(problem_to_summary_json(p));
    }

    send_ok(res, {
        {"items", items},
        {"total", result.total},
        {"page", page},
        {"page_size", page_size}
    });
  });

  // ── GET /api/v1/problems/:id ─────────────────────────────────
  // 题目详情 + 可见样例（is_sample=true）
  server.Get(R"(/api/v1/problems/(\d+))", [](const httplib::Request& req,
                                              httplib::Response& res) {
    int64_t problem_id = std::stoll(req.matches[1]);

    auto guard = ConnectionPool::instance().acquire();
    if (!guard) {
      LOG_ERROR("ProblemHandler::detail: failed to acquire DB connection");
      return send_error(res, 503, "database unavailable");
    }

    auto problem_opt = ProblemDAO::FindById(*guard, problem_id);
    if (!problem_opt.has_value()) {
      return send_error(res, 404, "problem not found");
    }

    // 查询测试用例（仅返回 is_sample=true 的）
    auto test_cases = TestCaseDAO::FindByProblemId(*guard, problem_id);

    send_ok(res, problem_to_detail_json(*problem_opt, test_cases));
  });

  LOG_INFO("ProblemHandler routes registered");
}

}  // namespace vibeoj
