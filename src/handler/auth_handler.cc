// 认证处理器 — 注册、登录、登出、refresh token。
#include "handler/auth_handler.h"

#include <openssl/evp.h>

#include "auth/jwt.h"
#include "auth/middleware.h"
#include "auth/password.h"
#include "common/log.h"
#include "config/config.h"
#include "db/connection_pool.h"
#include "db/dao.h"
#include "httplib.h"
#include "model/user.h"
#include "nlohmann/json.hpp"

namespace vibeoj {

using json = nlohmann::json;

namespace {

// 计算字符串的 SHA256 哈希（hex 格式，用于 refresh_token 存储）。
std::string sha256_hex(const std::string& data) {
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len = 0;

  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
  EVP_DigestUpdate(ctx, data.data(), data.size());
  EVP_DigestFinal_ex(ctx, hash, &hash_len);
  EVP_MD_CTX_free(ctx);

  std::string hex;
  hex.reserve(hash_len * 2);
  for (unsigned int i = 0; i < hash_len; i++) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", hash[i]);
    hex += buf;
  }
  return hex;
}

// 从 request body 解析 JSON，解析失败返回 false。
bool parse_json_body(const httplib::Request& req, json& out) {
  try {
    out = json::parse(req.body);
    return true;
  } catch (...) {
    return false;
  }
}

// 发送 JSON 错误响应。
void send_error(httplib::Response& res, int status, const std::string& msg) {
  res.status = status;
  res.set_content(json{{"error", msg}}.dump(), "application/json");
}

// 发送 JSON 成功响应。
void send_json(httplib::Response& res, const json& j, int status = 200) {
  res.status = status;
  res.set_content(j.dump(), "application/json");
}

// 设置 refresh_token 为 httpOnly cookie。
void set_refresh_cookie(httplib::Response& res, const std::string& token,
                        int ttl_seconds) {
  // cookie: refresh_token=<token>; HttpOnly; Path=/api/v1/auth; Max-Age=<ttl>; SameSite=Strict
  std::string cookie = "refresh_token=" + token +
      "; HttpOnly; Path=/api/v1/auth; Max-Age=" + std::to_string(ttl_seconds) +
      "; SameSite=Strict";
  res.set_header("Set-Cookie", cookie);
}

}  // namespace

void AuthHandler::register_routes(httplib::Server& server, const ServerConfig& cfg) {
  // 复制需要的配置字段（lambda 捕获）
  std::string jwt_secret = cfg.jwt_secret;
  int access_ttl  = cfg.jwt_access_ttl;
  int refresh_ttl = cfg.jwt_refresh_ttl;

  // ── POST /api/v1/auth/register ─────────────────────────────────
  server.Post("/api/v1/auth/register",
      [jwt_secret, access_ttl, refresh_ttl](const httplib::Request& req,
                                             httplib::Response& res) {
    json body;
    if (!parse_json_body(req, body)) {
      return send_error(res, 400, "invalid JSON body");
    }

    std::string username = body.value("username", "");
    std::string password = body.value("password", "");

    // 校验用户名长度
    if (username.empty() || username.size() > 64) {
      return send_error(res, 400, "username must be 1-64 characters");
    }
    // 校验密码长度
    if (password.size() < 6 || password.size() > 128) {
      return send_error(res, 400, "password must be 6-128 characters");
    }

    auto guard = ConnectionPool::instance().acquire();
    if (!guard) {
      LOG_ERROR("AuthHandler::register: failed to acquire DB connection");
      return send_error(res, 503, "database unavailable");
    }

    // 检查用户名是否已存在
    auto existing = UserDAO::FindByUsername(*guard, username);
    if (existing.has_value()) {
      return send_error(res, 409, "username already exists");
    }

    // 哈希密码并创建用户
    std::string hash = hash_password(password);
    if (hash.empty()) {
      LOG_ERROR("AuthHandler::register: bcrypt hash failed");
      return send_error(res, 500, "internal error");
    }

    int64_t uid = UserDAO::Create(*guard, username, hash);
    if (uid <= 0) {
      return send_error(res, 500, "failed to create user");
    }

    LOG_INFO("User registered: %s (id=%lld)", username.c_str(),
             static_cast<long long>(uid));
    send_json(res, {
        {"id", uid},
        {"username", username},
        {"role", "user"}
    }, 201);
  });

  // ── POST /api/v1/auth/login ────────────────────────────────────
  server.Post("/api/v1/auth/login",
      [jwt_secret, access_ttl, refresh_ttl](const httplib::Request& req,
                                             httplib::Response& res) {
    json body;
    if (!parse_json_body(req, body)) {
      return send_error(res, 400, "invalid JSON body");
    }

    std::string username = body.value("username", "");
    std::string password = body.value("password", "");

    if (username.empty() || password.empty()) {
      return send_error(res, 400, "username and password required");
    }

    auto guard = ConnectionPool::instance().acquire();
    if (!guard) {
      return send_error(res, 503, "database unavailable");
    }

    // 查找用户
    auto user_opt = UserDAO::FindByUsername(*guard, username);
    if (!user_opt.has_value()) {
      return send_error(res, 401, "invalid username or password");
    }

    const User& user = *user_opt;

    // 检查账号状态
    if (user.status == UserStatus::disabled) {
      return send_error(res, 403, "account is disabled");
    }

    // 验证密码
    if (!verify_password(password, user.password_hash)) {
      return send_error(res, 401, "invalid username or password");
    }

    // 生成 token
    std::string access_token  = generate_access_token(user, jwt_secret);
    std::string refresh_token = generate_refresh_token(user, jwt_secret);

    // refresh_token 哈希后存入 DB
    std::string token_hash = sha256_hex(refresh_token);
    time_t exp_time = std::time(nullptr) + refresh_ttl;
    char exp_buf[32];
    struct tm tm_buf;
    gmtime_r(&exp_time, &tm_buf);
    strftime(exp_buf, sizeof(exp_buf), "%Y-%m-%d %H:%M:%S", &tm_buf);

    RefreshTokenDAO::Create(*guard, user.id, token_hash, exp_buf);

    // 设置 cookie
    set_refresh_cookie(res, refresh_token, refresh_ttl);

    LOG_INFO("User logged in: %s (id=%lld)", username.c_str(),
             static_cast<long long>(user.id));
    send_json(res, {
        {"access_token", access_token},
        {"token_type", "Bearer"},
        {"expires_in", access_ttl},
        {"user", {
            {"id", user.id},
            {"username", user.username},
            {"role", user_role_to_string(user.role)}
        }}
    });
  });

  // ── POST /api/v1/auth/refresh ──────────────────────────────────
  server.Post("/api/v1/auth/refresh",
      [jwt_secret, access_ttl](const httplib::Request& req,
                                httplib::Response& res) {
    // 从 cookie 中读取 refresh_token
    std::string cookie_token;
    if (req.has_header("Cookie")) {
      std::string cookie_header = req.get_header_value("Cookie");
      const std::string key = "refresh_token=";
      auto pos = cookie_header.find(key);
      if (pos != std::string::npos) {
        pos += key.size();
        auto end = cookie_header.find(';', pos);
        cookie_token = cookie_header.substr(pos, end - pos);
      }
    }

    if (cookie_token.empty()) {
      return send_error(res, 401, "no refresh token");
    }

    // 验证 refresh_token JWT
    User token_user;
    if (!verify_token(cookie_token, jwt_secret, token_user)) {
      return send_error(res, 401, "invalid or expired refresh token");
    }

    // 检查 refresh_token 是否已被撤销（存在 DB 中）
    auto guard = ConnectionPool::instance().acquire();
    if (!guard) {
      return send_error(res, 503, "database unavailable");
    }

    std::string token_hash = sha256_hex(cookie_token);
    auto stored = RefreshTokenDAO::FindByHash(*guard, token_hash);
    if (!stored.has_value()) {
      return send_error(res, 401, "refresh token revoked");
    }

    // 获取完整用户信息以生成新 token
    auto user_opt = UserDAO::FindById(*guard, token_user.id);
    if (!user_opt.has_value()) {
      return send_error(res, 401, "user not found");
    }

    if (user_opt->status == UserStatus::disabled) {
      return send_error(res, 403, "account is disabled");
    }

    // 签发新的 access_token
    std::string new_access = generate_access_token(*user_opt, jwt_secret);

    send_json(res, {
        {"access_token", new_access},
        {"token_type", "Bearer"},
        {"expires_in", access_ttl}
    });
  });

  // ── POST /api/v1/auth/logout ───────────────────────────────────
  server.Post("/api/v1/auth/logout",
      [jwt_secret](const httplib::Request& req, httplib::Response& res) {
    // 从 cookie 中读取 refresh_token 并删除
    std::string cookie_token;
    if (req.has_header("Cookie")) {
      std::string cookie_header = req.get_header_value("Cookie");
      const std::string key = "refresh_token=";
      auto pos = cookie_header.find(key);
      if (pos != std::string::npos) {
        pos += key.size();
        auto end = cookie_header.find(';', pos);
        cookie_token = cookie_header.substr(pos, end - pos);
      }
    }

    if (!cookie_token.empty()) {
      auto guard = ConnectionPool::instance().acquire();
      if (guard) {
        std::string token_hash = sha256_hex(cookie_token);
        RefreshTokenDAO::DeleteByHash(*guard, token_hash);
      }
    }

    // 清除 cookie
    res.set_header("Set-Cookie",
        "refresh_token=; HttpOnly; Path=/api/v1/auth; Max-Age=0; SameSite=Strict");

    send_json(res, {{"message", "logged out"}});
  });
}

}  // namespace vibeoj
