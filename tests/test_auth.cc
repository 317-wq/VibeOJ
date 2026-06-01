// 认证模块单元测试 — 测试 bcrypt 密码哈希、JWT 签发/验证、
// 中间件 Bearer token 提取，以及 auth API 端点集成测试。
//
// 密码与 JWT 测试无需数据库。API 集成测试需要本地 MySQL 运行。
// 测试自动清理创建的用户数据。

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>

#include "auth/jwt.h"
#include "auth/middleware.h"
#include "auth/password.h"
#include "handler/auth_handler.h"
#include "common/log.h"
#include "config/config.h"
#include "db/connection_pool.h"
#include "db/dao.h"
#include "model/user.h"

// httplib for API integration tests
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

// ── Password 测试 ─────────────────────────────────────────────

TEST(PasswordTest, HashReturnsNonEmpty) {
  std::string hash = hash_password("testpassword");
  EXPECT_FALSE(hash.empty());
}

TEST(PasswordTest, HashStartsWithBcryptPrefix) {
  std::string hash = hash_password("testpassword");
  // bcrypt hash 以 $2b$<cost>$ 开头
  EXPECT_TRUE(hash.rfind("$2b$", 0) == 0) << "Hash: " << hash;
}

TEST(PasswordTest, HashProducesDifferentSalts) {
  std::string h1 = hash_password("same_password");
  std::string h2 = hash_password("same_password");
  // 相同密码应产生不同的哈希（随机盐）
  EXPECT_NE(h1, h2);
}

TEST(PasswordTest, VerifyCorrectPassword) {
  std::string hash = hash_password("mypassword123");
  ASSERT_FALSE(hash.empty());
  EXPECT_TRUE(verify_password("mypassword123", hash));
}

TEST(PasswordTest, VerifyWrongPassword) {
  std::string hash = hash_password("correct_password");
  ASSERT_FALSE(hash.empty());
  EXPECT_FALSE(verify_password("wrong_password", hash));
}

TEST(PasswordTest, VerifyEmptyPassword) {
  std::string hash = hash_password("somepass");
  ASSERT_FALSE(hash.empty());
  EXPECT_FALSE(verify_password("", hash));
}

TEST(PasswordTest, VerifyWithCorruptedHash) {
  EXPECT_FALSE(verify_password("test", "not_a_valid_hash"));
  EXPECT_FALSE(verify_password("test", ""));
}

// ── JWT 测试 ──────────────────────────────────────────────────

class JwtTest : public ::testing::Test {
 protected:
  void SetUp() override {
    secret_ = "test-jwt-secret-key";
    user_.id = 42;
    user_.username = "testuser";
    user_.role = UserRole::user;
  }

  std::string secret_;
  User user_;
};

TEST_F(JwtTest, GenerateAccessTokenReturnsNonEmpty) {
  std::string token = generate_access_token(user_, secret_);
  EXPECT_FALSE(token.empty());
}

TEST_F(JwtTest, GenerateRefreshTokenReturnsNonEmpty) {
  std::string token = generate_refresh_token(user_, secret_);
  EXPECT_FALSE(token.empty());
}

TEST_F(JwtTest, TokenHasThreeParts) {
  std::string token = generate_access_token(user_, secret_);
  // JWT 格式: header.payload.signature
  int dots = 0;
  for (char c : token) {
    if (c == '.') dots++;
  }
  EXPECT_EQ(dots, 2);
}

TEST_F(JwtTest, VerifyValidToken) {
  std::string token = generate_access_token(user_, secret_);
  ASSERT_FALSE(token.empty());

  User extracted;
  EXPECT_TRUE(verify_token(token, secret_, extracted));
}

TEST_F(JwtTest, VerifyTokenExtractsUserInfo) {
  std::string token = generate_access_token(user_, secret_);
  User extracted;
  ASSERT_TRUE(verify_token(token, secret_, extracted));

  EXPECT_EQ(extracted.id, user_.id);
  EXPECT_EQ(extracted.username, user_.username);
  EXPECT_EQ(extracted.role, user_.role);
}

TEST_F(JwtTest, VerifyTokenFailsWithWrongSecret) {
  std::string token = generate_access_token(user_, secret_);
  User extracted;
  EXPECT_FALSE(verify_token(token, "wrong-secret", extracted));
}

TEST_F(JwtTest, VerifyTokenFailsWithTamperedToken) {
  std::string token = generate_access_token(user_, secret_);
  // 篡改 payload 部分（修改中间段的一个字符）
  auto dot1 = token.find('.');
  auto dot2 = token.find('.', dot1 + 1);
  std::string tampered = token.substr(0, dot1 + 2) + "X" + token.substr(dot1 + 3);

  User extracted;
  EXPECT_FALSE(verify_token(tampered, secret_, extracted));
}

TEST_F(JwtTest, VerifyTokenFailsWithExpiredToken) {
  // 使用负 TTL 生成一个已过期的 token
  // generate_jwt 接受自定义 ttl
  std::string expired = generate_jwt(user_, secret_, -1);  // 1 秒前过期

  User extracted;
  EXPECT_FALSE(verify_token(expired, secret_, extracted));
}

TEST_F(JwtTest, VerifyTokenFailsWithEmptyToken) {
  User extracted;
  EXPECT_FALSE(verify_token("", secret_, extracted));
}

TEST_F(JwtTest, VerifyTokenFailsWithMalformedToken) {
  User extracted;
  EXPECT_FALSE(verify_token("not.a.jwt.token.extra", secret_, extracted));
  EXPECT_FALSE(verify_token("no_dots_here", secret_, extracted));
}

TEST_F(JwtTest, AccessTokenHasDifferentContentThanRefresh) {
  std::string access = generate_access_token(user_, secret_);
  std::string refresh = generate_refresh_token(user_, secret_);
  EXPECT_NE(access, refresh);
}

TEST_F(JwtTest, AdminRoleIsPreserved) {
  user_.role = UserRole::admin;
  std::string token = generate_access_token(user_, secret_);
  User extracted;
  ASSERT_TRUE(verify_token(token, secret_, extracted));
  EXPECT_EQ(extracted.role, UserRole::admin);
}

// ── Middleware 测试 ───────────────────────────────────────────

class MiddlewareTest : public ::testing::Test {
 protected:
  void SetUp() override {
    secret_ = "middleware-test-secret";
    user_.id = 99;
    user_.username = "mwuser";
    user_.role = UserRole::user;
    valid_token_ = generate_access_token(user_, secret_);
  }

  std::string secret_;
  User user_;
  std::string valid_token_;
};

TEST_F(MiddlewareTest, AuthenticateRequestWithValidBearerToken) {
  std::string header = "Bearer " + valid_token_;
  User extracted;
  EXPECT_TRUE(authenticate_request(header, secret_, extracted));
  EXPECT_EQ(extracted.id, user_.id);
}

TEST_F(MiddlewareTest, AuthenticateRequestFailsWithEmptyHeader) {
  User extracted;
  EXPECT_FALSE(authenticate_request("", secret_, extracted));
}

TEST_F(MiddlewareTest, AuthenticateRequestFailsWithoutBearerPrefix) {
  User extracted;
  EXPECT_FALSE(authenticate_request(valid_token_, secret_, extracted));
}

TEST_F(MiddlewareTest, AuthenticateRequestFailsWithWrongPrefix) {
  User extracted;
  EXPECT_FALSE(authenticate_request("Basic " + valid_token_, secret_, extracted));
}

TEST_F(MiddlewareTest, AuthenticateRequestFailsWithInvalidToken) {
  User extracted;
  EXPECT_FALSE(authenticate_request("Bearer invalid_token", secret_, extracted));
}

TEST_F(MiddlewareTest, RequireAdminForRegularUser) {
  EXPECT_FALSE(require_admin(user_));
}

TEST_F(MiddlewareTest, RequireAdminForAdminUser) {
  user_.role = UserRole::admin;
  EXPECT_TRUE(require_admin(user_));
}

// ── Auth API 集成测试夹具 ─────────────────────────────────────

class AuthApiTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    // 初始化日志
    Logger::instance().init("logs");

    // 加载配置
    ServerConfig cfg = load_config();
    jwt_secret_ = cfg.jwt_secret;

    // 初始化连接池
    ConnectionPool::instance().init(cfg);

    // 启动测试服务器（使用随机端口避免冲突）
    cfg.port = 18080;
    server_ = std::make_unique<httplib::Server>();

    // 注册认证路由
    vibeoj::AuthHandler::register_routes(*server_, cfg);

    // 在独立线程启动服务器
    server_thread_ = std::thread([cfg]() {
      server_->listen("127.0.0.1", cfg.port);
    });

    // 等待服务器就绪
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 初始化 HTTP 客户端
    client_ = std::make_unique<httplib::Client>("http://127.0.0.1:18080");
  }

  static void TearDownTestSuite() {
    server_->stop();
    if (server_thread_.joinable()) {
      server_thread_.join();
    }
    client_.reset();
    server_.reset();
    ConnectionPool::instance().shutdown();
    Logger::instance().close();
  }

  void TearDown() override {
    // 清理测试用户
    for (auto username : created_users_) {
      auto guard = ConnectionPool::instance().acquire();
      if (guard) {
        auto u = UserDAO::FindByUsername(*guard, username);
        if (u.has_value()) {
          RefreshTokenDAO::DeleteByUserId(*guard, u->id);
          UserDAO::Delete(*guard, u->id);
        }
      }
    }
    created_users_.clear();
    cookies_.clear();
  }

  // 注册一个测试用户，返回响应
  httplib::Result register_user(const std::string& username,
                                 const std::string& password) {
    created_users_.push_back(username);
    json body = {{"username", username}, {"password", password}};
    return client_->Post("/api/v1/auth/register",
                         {{"Content-Type", "application/json"}},
                         body.dump(), "application/json");
  }

  // 登录，返回响应。cookies 自动保存。
  httplib::Result login_user(const std::string& username,
                              const std::string& password) {
    json body = {{"username", username}, {"password", password}};
    auto res = client_->Post("/api/v1/auth/login",
                            {{"Content-Type", "application/json"}},
                            body.dump(), "application/json");
    if (res && res->status == 200) {
      // 提取 Set-Cookie
      auto it = res->headers.find("Set-Cookie");
      if (it != res->headers.end()) {
        cookies_ = it->second;
      }
    }
    return res;
  }

  // 使用保存的 cookie 刷新 token
  httplib::Result refresh_token() {
    httplib::Headers headers;
    if (!cookies_.empty()) {
      headers.emplace("Cookie", cookies_);
    }
    return client_->Post("/api/v1/auth/refresh", headers, "", "");
  }

  // 使用保存的 cookie 登出
  httplib::Result logout() {
    httplib::Headers headers;
    if (!cookies_.empty()) {
      headers.emplace("Cookie", cookies_);
    }
    return client_->Post("/api/v1/auth/logout", headers, "", "");
  }

  static std::unique_ptr<httplib::Server> server_;
  static std::unique_ptr<httplib::Client> client_;
  static std::thread server_thread_;
  static std::string jwt_secret_;

  std::vector<std::string> created_users_;
  std::string cookies_;
};

std::unique_ptr<httplib::Server> AuthApiTest::server_;
std::unique_ptr<httplib::Client> AuthApiTest::client_;
std::thread AuthApiTest::server_thread_;
std::string AuthApiTest::jwt_secret_;

// ── 注册测试 ──────────────────────────────────────────────────

TEST_F(AuthApiTest, RegisterSuccess) {
  auto res = register_user("api_test_register", "pass123456");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 201);

  auto j = json::parse(res->body);
  EXPECT_EQ(j["username"], "api_test_register");
  EXPECT_EQ(j["role"], "user");
  EXPECT_GT(j["id"], 0);
}

TEST_F(AuthApiTest, RegisterDuplicateUsername) {
  register_user("api_test_dup", "pass123456");
  auto res = register_user("api_test_dup", "pass123456");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 409);
}

TEST_F(AuthApiTest, RegisterEmptyUsername) {
  auto res = register_user("", "pass123456");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 400);
}

TEST_F(AuthApiTest, RegisterShortPassword) {
  auto res = register_user("api_test_short", "12345");  // < 6 chars
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 400);
}

// ── 登录测试 ──────────────────────────────────────────────────

TEST_F(AuthApiTest, LoginSuccess) {
  register_user("api_test_login", "mypassword");
  auto res = login_user("api_test_login", "mypassword");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 200);

  auto j = json::parse(res->body);
  EXPECT_FALSE(j["access_token"].get<std::string>().empty());
  EXPECT_EQ(j["token_type"], "Bearer");
  EXPECT_EQ(j["user"]["username"], "api_test_login");

  // 验证 Set-Cookie 存在
  auto it = res->headers.find("Set-Cookie");
  EXPECT_NE(it, res->headers.end());
  EXPECT_NE(it->second.find("refresh_token="), std::string::npos);
  EXPECT_NE(it->second.find("HttpOnly"), std::string::npos);
}

TEST_F(AuthApiTest, LoginWrongPassword) {
  register_user("api_test_wp", "correctpass");
  auto res = login_user("api_test_wp", "wrongpass");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 401);
}

TEST_F(AuthApiTest, LoginNonExistentUser) {
  auto res = login_user("no_such_user_xyz", "anypass");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 401);
}

TEST_F(AuthApiTest, LoginDisabledAccount) {
  register_user("api_test_disabled", "pass123");
  // 手动禁用账号
  {
    auto guard = ConnectionPool::instance().acquire();
    ASSERT_TRUE(guard);
    auto u = UserDAO::FindByUsername(*guard, "api_test_disabled");
    ASSERT_TRUE(u.has_value());
    UserDAO::UpdateStatus(*guard, u->id, UserStatus::disabled);
  }

  auto res = login_user("api_test_disabled", "pass123");
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 403);
}

// ── Refresh 测试 ──────────────────────────────────────────────

TEST_F(AuthApiTest, RefreshSuccess) {
  register_user("api_test_refresh", "pass123");
  auto login_res = login_user("api_test_refresh", "pass123");
  ASSERT_TRUE(login_res != nullptr);
  ASSERT_EQ(login_res->status, 200);

  auto refresh_res = refresh_token();
  ASSERT_TRUE(refresh_res != nullptr);
  EXPECT_EQ(refresh_res->status, 200);

  auto j = json::parse(refresh_res->body);
  EXPECT_FALSE(j["access_token"].get<std::string>().empty());
}

TEST_F(AuthApiTest, RefreshWithoutCookie) {
  cookies_.clear();
  auto res = refresh_token();
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(res->status, 401);
}

// ── 登出测试 ──────────────────────────────────────────────────

TEST_F(AuthApiTest, LogoutSuccess) {
  register_user("api_test_logout", "pass123");
  auto login_res = login_user("api_test_logout", "pass123");
  ASSERT_TRUE(login_res != nullptr);
  ASSERT_EQ(login_res->status, 200);

  auto logout_res = logout();
  ASSERT_TRUE(logout_res != nullptr);
  EXPECT_EQ(logout_res->status, 200);
}

TEST_F(AuthApiTest, RefreshAfterLogoutFails) {
  register_user("api_test_ral", "pass123");
  auto login_res = login_user("api_test_ral", "pass123");
  ASSERT_TRUE(login_res != nullptr);
  ASSERT_EQ(login_res->status, 200);

  // 登出
  logout();

  // 刷新应失败（token 已撤销）
  auto refresh_res = refresh_token();
  ASSERT_TRUE(refresh_res != nullptr);
  EXPECT_EQ(refresh_res->status, 401);
}

// ── 密码哈希 API 验证 ─────────────────────────────────────────

TEST_F(AuthApiTest, RegisteredPasswordIsBcryptHashed) {
  register_user("api_test_hash", "securepass");
  auto guard = ConnectionPool::instance().acquire();
  ASSERT_TRUE(guard);
  auto u = UserDAO::FindByUsername(*guard, "api_test_hash");
  ASSERT_TRUE(u.has_value());
  // 数据库中存储的应是 bcrypt 哈希，不是明文
  EXPECT_TRUE(u->password_hash.rfind("$2b$", 0) == 0);
  EXPECT_NE(u->password_hash, "securepass");
}

}  // namespace
}  // namespace vibeoj
