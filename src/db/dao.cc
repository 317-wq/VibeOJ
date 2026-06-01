// DAO 实现 — 使用 prepared statement 防止 SQL 注入，所有操作以 sql::Connection& 为参数。
#include "db/dao.h"

#include <cppconn/connection.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include <cppconn/exception.h>
#include <cppconn/sqlstring.h>

#include "common/log.h"
#include "model/user.h"
#include "model/problem.h"
#include "model/submission.h"
#include "model/refresh_token.h"

namespace vibeoj {

namespace {
// 从 ResultSet 安全读取字符串（可为 NULL 返回空串）。
inline std::string safe_str(sql::ResultSet* rs, uint32_t col) {
  if (rs->isNull(col)) return "";
  return rs->getString(col).asStdString();
}

// 安全读取 int64_t（可为 NULL 返回 0）。
inline int64_t safe_int64(sql::ResultSet* rs, uint32_t col) {
  if (rs->isNull(col)) return 0;
  return rs->getInt64(col);
}

// 获取最近 INSERT 的自增 ID（mysql-connector-cpp 1.1.12 无 getGeneratedKeys）。
int64_t last_insert_id(sql::Connection& conn) {
  auto stmt = conn.createStatement();
  auto rs = stmt->executeQuery("SELECT LAST_INSERT_ID()");
  int64_t id = -1;
  if (rs->next()) {
    id = rs->getInt64(1);
  }
  delete rs;
  delete stmt;
  return id;
}
}  // namespace

// ── UserDAO ─────────────────────────────────────────────────────

int64_t UserDAO::Create(sql::Connection& conn, const std::string& username,
                         const std::string& password_hash, UserRole role) {
  try {
    auto stmt = conn.prepareStatement(
        "INSERT INTO users (username, password_hash, role) VALUES (?, ?, ?)");
    stmt->setString(1, sql::SQLString(username));
    stmt->setString(2, sql::SQLString(password_hash));
    stmt->setString(3, sql::SQLString(user_role_to_string(role)));
    stmt->executeUpdate();
    delete stmt;

    return last_insert_id(conn);
  } catch (const sql::SQLException& e) {
    LOG_ERROR("UserDAO::Create: %s", e.what());
    return -1;
  }
}

std::optional<User> UserDAO::FindByUsername(sql::Connection& conn,
                                            const std::string& username) {
  try {
    auto stmt = conn.prepareStatement(
        "SELECT id, username, password_hash, role, status, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') AS created_at "
        "FROM users WHERE username = ?");
    stmt->setString(1, sql::SQLString(username));
    auto rs = stmt->executeQuery();

    std::optional<User> result;
    if (rs->next()) {
      User u;
      u.id            = rs->getInt64("id");
      u.username      = rs->getString("username").asStdString();
      u.password_hash = rs->getString("password_hash").asStdString();
      u.role  = user_role_from_string(rs->getString("role").asStdString());
      u.status= user_status_from_string(rs->getString("status").asStdString());
      u.created_at    = rs->getString("created_at").asStdString();
      result = std::move(u);
    }
    delete rs;
    delete stmt;
    return result;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("UserDAO::FindByUsername: %s", e.what());
    return std::nullopt;
  }
}

std::optional<User> UserDAO::FindById(sql::Connection& conn, int64_t id) {
  try {
    auto stmt = conn.prepareStatement(
        "SELECT id, username, password_hash, role, status, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') AS created_at "
        "FROM users WHERE id = ?");
    stmt->setInt64(1, id);
    auto rs = stmt->executeQuery();

    std::optional<User> result;
    if (rs->next()) {
      User u;
      u.id            = rs->getInt64("id");
      u.username      = rs->getString("username").asStdString();
      u.password_hash = rs->getString("password_hash").asStdString();
      u.role  = user_role_from_string(rs->getString("role").asStdString());
      u.status= user_status_from_string(rs->getString("status").asStdString());
      u.created_at    = rs->getString("created_at").asStdString();
      result = std::move(u);
    }
    delete rs;
    delete stmt;
    return result;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("UserDAO::FindById: %s", e.what());
    return std::nullopt;
  }
}

bool UserDAO::UpdateRole(sql::Connection& conn, int64_t id, UserRole role) {
  try {
    auto stmt = conn.prepareStatement("UPDATE users SET role = ? WHERE id = ?");
    stmt->setString(1, sql::SQLString(user_role_to_string(role)));
    stmt->setInt64(2, id);
    int affected = stmt->executeUpdate();
    delete stmt;
    return affected > 0;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("UserDAO::UpdateRole: %s", e.what());
    return false;
  }
}

bool UserDAO::UpdateStatus(sql::Connection& conn, int64_t id, UserStatus status) {
  try {
    auto stmt = conn.prepareStatement(
        "UPDATE users SET status = ? WHERE id = ?");
    stmt->setString(1, sql::SQLString(user_status_to_string(status)));
    stmt->setInt64(2, id);
    int affected = stmt->executeUpdate();
    delete stmt;
    return affected > 0;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("UserDAO::UpdateStatus: %s", e.what());
    return false;
  }
}

std::vector<User> UserDAO::List(sql::Connection& conn) {
  std::vector<User> users;
  try {
    auto stmt = conn.createStatement();
    auto rs = stmt->executeQuery(
        "SELECT id, username, password_hash, role, status, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') AS created_at "
        "FROM users ORDER BY id");
    while (rs->next()) {
      User u;
      u.id            = rs->getInt64("id");
      u.username      = rs->getString("username").asStdString();
      u.password_hash = rs->getString("password_hash").asStdString();
      u.role  = user_role_from_string(rs->getString("role").asStdString());
      u.status= user_status_from_string(rs->getString("status").asStdString());
      u.created_at    = rs->getString("created_at").asStdString();
      users.push_back(std::move(u));
    }
    delete rs;
    delete stmt;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("UserDAO::List: %s", e.what());
  }
  return users;
}

bool UserDAO::Delete(sql::Connection& conn, int64_t id) {
  try {
    auto stmt = conn.prepareStatement("DELETE FROM users WHERE id = ?");
    stmt->setInt64(1, id);
    int affected = stmt->executeUpdate();
    delete stmt;
    return affected > 0;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("UserDAO::Delete: %s", e.what());
    return false;
  }
}

// ── ProblemDAO ──────────────────────────────────────────────────

int64_t ProblemDAO::Create(sql::Connection& conn, Problem& problem) {
  try {
    auto stmt = conn.prepareStatement(
        "INSERT INTO problems (title, description, difficulty, "
        "time_limit_ms, memory_limit_kb, created_by) "
        "VALUES (?, ?, ?, ?, ?, ?)");
    stmt->setString(1, sql::SQLString(problem.title));
    stmt->setString(2, sql::SQLString(problem.description));
    stmt->setString(3, sql::SQLString(difficulty_to_string(problem.difficulty)));
    stmt->setInt(4, problem.time_limit_ms);
    stmt->setInt(5, problem.memory_limit_kb);
    if (problem.created_by > 0) {
      stmt->setInt64(6, problem.created_by);
    } else {
      stmt->setNull(6, sql::DataType::BIGINT);
    }
    stmt->executeUpdate();
    delete stmt;

    int64_t id = last_insert_id(conn);
    if (id > 0) problem.id = id;
    return id;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("ProblemDAO::Create: %s", e.what());
    return -1;
  }
}

std::optional<Problem> ProblemDAO::FindById(sql::Connection& conn, int64_t id) {
  try {
    auto stmt = conn.prepareStatement(
        "SELECT id, title, description, difficulty, time_limit_ms, "
        "memory_limit_kb, created_by, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') AS created_at "
        "FROM problems WHERE id = ?");
    stmt->setInt64(1, id);
    auto rs = stmt->executeQuery();

    std::optional<Problem> result;
    if (rs->next()) {
      Problem p;
      p.id          = rs->getInt64("id");
      p.title       = rs->getString("title").asStdString();
      p.description = safe_str(rs, 3);  // description (TEXT, 第 3 列)
      p.difficulty  = difficulty_from_string(
          rs->getString("difficulty").asStdString());
      p.time_limit_ms   = rs->getInt("time_limit_ms");
      p.memory_limit_kb = rs->getInt("memory_limit_kb");
      p.created_by  = safe_int64(rs, rs->findColumn("created_by"));
      p.created_at  = rs->getString("created_at").asStdString();
      result = std::move(p);
    }
    delete rs;
    delete stmt;
    return result;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("ProblemDAO::FindById: %s", e.what());
    return std::nullopt;
  }
}

ProblemDAO::ListResult ProblemDAO::FindAll(sql::Connection& conn,
                                           const std::string& difficulty,
                                           int page, int page_size) {
  ListResult result;
  try {
    // 构建查询
    std::string where;
    if (!difficulty.empty()) {
      where = " WHERE difficulty = '" + difficulty + "'";
    }

    // 查询总数
    {
      auto stmt = conn.createStatement();
      auto rs = stmt->executeQuery(
          "SELECT COUNT(*) FROM problems" + where);
      if (rs->next()) {
        result.total = rs->getInt64(1);
      }
      delete rs;
      delete stmt;
    }

    // 分页查询
    int offset = (page - 1) * page_size;
    auto stmt = conn.prepareStatement(
        "SELECT id, title, description, difficulty, time_limit_ms, "
        "memory_limit_kb, created_by, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') AS created_at "
        "FROM problems" + where + " ORDER BY id LIMIT ? OFFSET ?");
    stmt->setInt(1, page_size);
    stmt->setInt(2, offset);
    auto rs = stmt->executeQuery();

    while (rs->next()) {
      Problem p;
      p.id          = rs->getInt64("id");
      p.title       = rs->getString("title").asStdString();
      p.description = safe_str(rs, 3);
      p.difficulty  = difficulty_from_string(
          rs->getString("difficulty").asStdString());
      p.time_limit_ms   = rs->getInt("time_limit_ms");
      p.memory_limit_kb = rs->getInt("memory_limit_kb");
      p.created_by  = safe_int64(rs, rs->findColumn("created_by"));
      p.created_at  = rs->getString("created_at").asStdString();
      result.problems.push_back(std::move(p));
    }
    delete rs;
    delete stmt;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("ProblemDAO::FindAll: %s", e.what());
  }
  return result;
}

bool ProblemDAO::Update(sql::Connection& conn, const Problem& problem) {
  try {
    auto stmt = conn.prepareStatement(
        "UPDATE problems SET title = ?, description = ?, difficulty = ?, "
        "time_limit_ms = ?, memory_limit_kb = ? WHERE id = ?");
    stmt->setString(1, sql::SQLString(problem.title));
    stmt->setString(2, sql::SQLString(problem.description));
    stmt->setString(3,
        sql::SQLString(difficulty_to_string(problem.difficulty)));
    stmt->setInt(4, problem.time_limit_ms);
    stmt->setInt(5, problem.memory_limit_kb);
    stmt->setInt64(6, problem.id);
    int affected = stmt->executeUpdate();
    delete stmt;
    return affected > 0;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("ProblemDAO::Update: %s", e.what());
    return false;
  }
}

bool ProblemDAO::Delete(sql::Connection& conn, int64_t id) {
  try {
    auto stmt = conn.prepareStatement("DELETE FROM problems WHERE id = ?");
    stmt->setInt64(1, id);
    int affected = stmt->executeUpdate();
    delete stmt;
    return affected > 0;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("ProblemDAO::Delete: %s", e.what());
    return false;
  }
}

// ── TestCaseDAO ─────────────────────────────────────────────────

int64_t TestCaseDAO::Create(sql::Connection& conn, TestCase& tc) {
  try {
    auto stmt = conn.prepareStatement(
        "INSERT INTO test_cases (problem_id, input, expected_output, "
        "is_sample, order_index) VALUES (?, ?, ?, ?, ?)");
    stmt->setInt64(1, tc.problem_id);
    stmt->setString(2, sql::SQLString(tc.input));
    stmt->setString(3, sql::SQLString(tc.expected_output));
    stmt->setBoolean(4, tc.is_sample);
    stmt->setInt(5, tc.order_index);
    stmt->executeUpdate();
    delete stmt;

    int64_t id = last_insert_id(conn);
    if (id > 0) tc.id = id;
    return id;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("TestCaseDAO::Create: %s", e.what());
    return -1;
  }
}

std::vector<TestCase> TestCaseDAO::FindByProblemId(sql::Connection& conn,
                                                    int64_t problem_id) {
  std::vector<TestCase> cases;
  try {
    auto stmt = conn.prepareStatement(
        "SELECT id, problem_id, input, expected_output, is_sample, order_index "
        "FROM test_cases WHERE problem_id = ? ORDER BY order_index");
    stmt->setInt64(1, problem_id);
    auto rs = stmt->executeQuery();

    while (rs->next()) {
      TestCase tc;
      tc.id              = rs->getInt64("id");
      tc.problem_id      = rs->getInt64("problem_id");
      tc.input           = rs->getString("input").asStdString();
      tc.expected_output = rs->getString("expected_output").asStdString();
      tc.is_sample       = rs->getBoolean("is_sample");
      tc.order_index     = rs->getInt("order_index");
      cases.push_back(std::move(tc));
    }
    delete rs;
    delete stmt;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("TestCaseDAO::FindByProblemId: %s", e.what());
  }
  return cases;
}

bool TestCaseDAO::Update(sql::Connection& conn, const TestCase& tc) {
  try {
    auto stmt = conn.prepareStatement(
        "UPDATE test_cases SET input = ?, expected_output = ?, "
        "is_sample = ?, order_index = ? WHERE id = ?");
    stmt->setString(1, sql::SQLString(tc.input));
    stmt->setString(2, sql::SQLString(tc.expected_output));
    stmt->setBoolean(3, tc.is_sample);
    stmt->setInt(4, tc.order_index);
    stmt->setInt64(5, tc.id);
    int affected = stmt->executeUpdate();
    delete stmt;
    return affected > 0;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("TestCaseDAO::Update: %s", e.what());
    return false;
  }
}

bool TestCaseDAO::Delete(sql::Connection& conn, int64_t id) {
  try {
    auto stmt = conn.prepareStatement("DELETE FROM test_cases WHERE id = ?");
    stmt->setInt64(1, id);
    int affected = stmt->executeUpdate();
    delete stmt;
    return affected > 0;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("TestCaseDAO::Delete: %s", e.what());
    return false;
  }
}

int TestCaseDAO::DeleteByProblemId(sql::Connection& conn, int64_t problem_id) {
  try {
    auto stmt = conn.prepareStatement(
        "DELETE FROM test_cases WHERE problem_id = ?");
    stmt->setInt64(1, problem_id);
    int affected = stmt->executeUpdate();
    delete stmt;
    return affected;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("TestCaseDAO::DeleteByProblemId: %s", e.what());
    return 0;
  }
}

// ── SubmissionDAO ───────────────────────────────────────────────

int64_t SubmissionDAO::Create(sql::Connection& conn, Submission& sub) {
  try {
    auto stmt = conn.prepareStatement(
        "INSERT INTO submissions (user_id, problem_id, code) VALUES (?, ?, ?)");
    stmt->setInt64(1, sub.user_id);
    stmt->setInt64(2, sub.problem_id);
    stmt->setString(3, sql::SQLString(sub.code));
    stmt->executeUpdate();
    delete stmt;

    int64_t id = last_insert_id(conn);
    if (id > 0) sub.id = id;
    return id;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("SubmissionDAO::Create: %s", e.what());
    return -1;
  }
}

namespace {
// 辅助函数：从 ResultSet 读取一行的 Submission。
Submission load_submission(sql::ResultSet* rs) {
  Submission s;
  s.id             = rs->getInt64("id");
  s.user_id        = rs->getInt64("user_id");
  s.problem_id     = rs->getInt64("problem_id");
  s.code           = rs->getString("code").asStdString();
  s.status = submission_status_from_string(
      rs->getString("status").asStdString());
  s.compile_output = safe_str(rs, rs->findColumn("compile_output"));
  s.passed_cases   = rs->getInt("passed_cases");
  s.total_cases    = rs->getInt("total_cases");
  s.time_used_ms   = rs->getInt("time_used_ms");
  s.memory_used_kb = rs->getInt("memory_used_kb");
  s.diff_output    = safe_str(rs, rs->findColumn("diff_output"));
  s.created_at     = rs->getString("created_at").asStdString();
  s.updated_at     = rs->getString("updated_at").asStdString();
  return s;
}
}  // namespace

std::optional<Submission> SubmissionDAO::FindById(sql::Connection& conn,
                                                   int64_t id) {
  try {
    auto stmt = conn.prepareStatement(
        "SELECT id, user_id, problem_id, code, status, compile_output, "
        "passed_cases, total_cases, time_used_ms, memory_used_kb, "
        "diff_output, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') AS created_at, "
        "DATE_FORMAT(updated_at, '%Y-%m-%d %H:%i:%s') AS updated_at "
        "FROM submissions WHERE id = ?");
    stmt->setInt64(1, id);
    auto rs = stmt->executeQuery();

    std::optional<Submission> result;
    if (rs->next()) {
      result = load_submission(rs);
    }
    delete rs;
    delete stmt;
    return result;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("SubmissionDAO::FindById: %s", e.what());
    return std::nullopt;
  }
}

std::vector<Submission> SubmissionDAO::FindByUserId(sql::Connection& conn,
                                                     int64_t user_id) {
  std::vector<Submission> subs;
  try {
    auto stmt = conn.prepareStatement(
        "SELECT id, user_id, problem_id, code, status, compile_output, "
        "passed_cases, total_cases, time_used_ms, memory_used_kb, "
        "diff_output, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') AS created_at, "
        "DATE_FORMAT(updated_at, '%Y-%m-%d %H:%i:%s') AS updated_at "
        "FROM submissions WHERE user_id = ? ORDER BY id DESC");
    stmt->setInt64(1, user_id);
    auto rs = stmt->executeQuery();
    while (rs->next()) {
      subs.push_back(load_submission(rs));
    }
    delete rs;
    delete stmt;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("SubmissionDAO::FindByUserId: %s", e.what());
  }
  return subs;
}

std::vector<Submission> SubmissionDAO::FindByProblemId(sql::Connection& conn,
                                                        int64_t problem_id) {
  std::vector<Submission> subs;
  try {
    auto stmt = conn.prepareStatement(
        "SELECT id, user_id, problem_id, code, status, compile_output, "
        "passed_cases, total_cases, time_used_ms, memory_used_kb, "
        "diff_output, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') AS created_at, "
        "DATE_FORMAT(updated_at, '%Y-%m-%d %H:%i:%s') AS updated_at "
        "FROM submissions WHERE problem_id = ? ORDER BY id DESC");
    stmt->setInt64(1, problem_id);
    auto rs = stmt->executeQuery();
    while (rs->next()) {
      subs.push_back(load_submission(rs));
    }
    delete rs;
    delete stmt;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("SubmissionDAO::FindByProblemId: %s", e.what());
  }
  return subs;
}

std::vector<Submission> SubmissionDAO::FindByUserAndProblem(
    sql::Connection& conn, int64_t user_id, int64_t problem_id) {
  std::vector<Submission> subs;
  try {
    auto stmt = conn.prepareStatement(
        "SELECT id, user_id, problem_id, code, status, compile_output, "
        "passed_cases, total_cases, time_used_ms, memory_used_kb, "
        "diff_output, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') AS created_at, "
        "DATE_FORMAT(updated_at, '%Y-%m-%d %H:%i:%s') AS updated_at "
        "FROM submissions WHERE user_id = ? AND problem_id = ? "
        "ORDER BY id DESC");
    stmt->setInt64(1, user_id);
    stmt->setInt64(2, problem_id);
    auto rs = stmt->executeQuery();
    while (rs->next()) {
      subs.push_back(load_submission(rs));
    }
    delete rs;
    delete stmt;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("SubmissionDAO::FindByUserAndProblem: %s", e.what());
  }
  return subs;
}

SubmissionDAO::ListResult SubmissionDAO::List(sql::Connection& conn,
                                                   int64_t user_id,
                                                   int64_t problem_id,
                                                   int page, int page_size) {
  ListResult result;
  try {
    // 动态构建 WHERE 子句
    std::string where;
    if (user_id > 0 && problem_id > 0) {
      where = "WHERE user_id = ? AND problem_id = ?";
    } else if (user_id > 0) {
      where = "WHERE user_id = ?";
    } else if (problem_id > 0) {
      where = "WHERE problem_id = ?";
    }

    // COUNT 查询
    std::string count_sql = "SELECT COUNT(*) FROM submissions " + where;
    auto count_stmt = conn.prepareStatement(count_sql);
    int param_idx = 1;
    if (user_id > 0) count_stmt->setInt64(param_idx++, user_id);
    if (problem_id > 0) count_stmt->setInt64(param_idx++, problem_id);
    auto count_rs = count_stmt->executeQuery();
    if (count_rs->next()) result.total = count_rs->getInt64(1);
    delete count_rs;
    delete count_stmt;

    if (result.total == 0) return result;

    // 分页查询
    int offset = (page - 1) * page_size;
    std::string sql =
        "SELECT id, user_id, problem_id, code, status, compile_output, "
        "passed_cases, total_cases, time_used_ms, memory_used_kb, "
        "diff_output, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') AS created_at, "
        "DATE_FORMAT(updated_at, '%Y-%m-%d %H:%i:%s') AS updated_at "
        "FROM submissions " + where + " ORDER BY id DESC LIMIT ? OFFSET ?";
    auto stmt = conn.prepareStatement(sql);
    param_idx = 1;
    if (user_id > 0) stmt->setInt64(param_idx++, user_id);
    if (problem_id > 0) stmt->setInt64(param_idx++, problem_id);
    stmt->setInt(param_idx++, page_size);
    stmt->setInt(param_idx++, offset);
    auto rs = stmt->executeQuery();
    while (rs->next()) {
      result.submissions.push_back(load_submission(rs));
    }
    delete rs;
    delete stmt;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("SubmissionDAO::List: %s", e.what());
  }
  return result;
}

bool SubmissionDAO::UpdateStatus(sql::Connection& conn, int64_t id,
                                 SubmissionStatus status, int passed_cases,
                                 int total_cases, int time_ms, int memory_kb,
                                 const std::string& compile_output,
                                 const std::string& diff_output) {
  try {
    auto stmt = conn.prepareStatement(
        "UPDATE submissions SET status = ?, passed_cases = ?, "
        "total_cases = ?, time_used_ms = ?, memory_used_kb = ?, "
        "compile_output = ?, diff_output = ? WHERE id = ?");
    stmt->setString(1, sql::SQLString(submission_status_to_string(status)));
    stmt->setInt(2, passed_cases);
    stmt->setInt(3, total_cases);
    stmt->setInt(4, time_ms);
    stmt->setInt(5, memory_kb);
    stmt->setString(6, sql::SQLString(compile_output));
    stmt->setString(7, sql::SQLString(diff_output));
    stmt->setInt64(8, id);
    int affected = stmt->executeUpdate();
    delete stmt;
    return affected > 0;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("SubmissionDAO::UpdateStatus: %s", e.what());
    return false;
  }
}

bool SubmissionDAO::Delete(sql::Connection& conn, int64_t id) {
  try {
    auto stmt = conn.prepareStatement(
        "DELETE FROM submissions WHERE id = ?");
    stmt->setInt64(1, id);
    int affected = stmt->executeUpdate();
    delete stmt;
    return affected > 0;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("SubmissionDAO::Delete: %s", e.what());
    return false;
  }
}

// ── RefreshTokenDAO ─────────────────────────────────────────────

int64_t RefreshTokenDAO::Create(sql::Connection& conn, int64_t user_id,
                                 const std::string& token_hash,
                                 const std::string& expires_at) {
  try {
    auto stmt = conn.prepareStatement(
        "INSERT INTO refresh_tokens (user_id, token_hash, expires_at) "
        "VALUES (?, ?, ?)");
    stmt->setInt64(1, user_id);
    stmt->setString(2, sql::SQLString(token_hash));
    stmt->setString(3, sql::SQLString(expires_at));
    stmt->executeUpdate();
    delete stmt;

    return last_insert_id(conn);
  } catch (const sql::SQLException& e) {
    LOG_ERROR("RefreshTokenDAO::Create: %s", e.what());
    return -1;
  }
}

std::optional<RefreshToken> RefreshTokenDAO::FindByHash(
    sql::Connection& conn, const std::string& token_hash) {
  try {
    auto stmt = conn.prepareStatement(
        "SELECT id, user_id, token_hash, "
        "DATE_FORMAT(expires_at, '%Y-%m-%d %H:%i:%s') AS expires_at, "
        "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s') AS created_at "
        "FROM refresh_tokens WHERE token_hash = ?");
    stmt->setString(1, sql::SQLString(token_hash));
    auto rs = stmt->executeQuery();

    std::optional<RefreshToken> result;
    if (rs->next()) {
      RefreshToken rt;
      rt.id         = rs->getInt64("id");
      rt.user_id    = rs->getInt64("user_id");
      rt.token_hash = rs->getString("token_hash").asStdString();
      rt.expires_at = rs->getString("expires_at").asStdString();
      rt.created_at = rs->getString("created_at").asStdString();
      result = std::move(rt);
    }
    delete rs;
    delete stmt;
    return result;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("RefreshTokenDAO::FindByHash: %s", e.what());
    return std::nullopt;
  }
}

int RefreshTokenDAO::DeleteByUserId(sql::Connection& conn, int64_t user_id) {
  try {
    auto stmt = conn.prepareStatement(
        "DELETE FROM refresh_tokens WHERE user_id = ?");
    stmt->setInt64(1, user_id);
    int affected = stmt->executeUpdate();
    delete stmt;
    return affected;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("RefreshTokenDAO::DeleteByUserId: %s", e.what());
    return 0;
  }
}

int RefreshTokenDAO::DeleteExpired(sql::Connection& conn) {
  try {
    auto stmt = conn.createStatement();
    int affected = stmt->executeUpdate(
        "DELETE FROM refresh_tokens WHERE expires_at < NOW()");
    delete stmt;
    return affected;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("RefreshTokenDAO::DeleteExpired: %s", e.what());
    return 0;
  }
}

bool RefreshTokenDAO::DeleteByHash(sql::Connection& conn,
                                    const std::string& token_hash) {
  try {
    auto stmt = conn.prepareStatement(
        "DELETE FROM refresh_tokens WHERE token_hash = ?");
    stmt->setString(1, sql::SQLString(token_hash));
    int affected = stmt->executeUpdate();
    delete stmt;
    return affected > 0;
  } catch (const sql::SQLException& e) {
    LOG_ERROR("RefreshTokenDAO::DeleteByHash: %s", e.what());
    return false;
  }
}

}  // namespace vibeoj
