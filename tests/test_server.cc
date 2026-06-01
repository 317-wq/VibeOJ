// HTTP 服务器配置模块的单元测试 — 测试 ServerConfig 加载与环境变量解析。
// 服务器端到端集成测试通过手动运行 vibeoj-server 并 curl 验证。

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "config/config.h"

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

// ── 默认值测试 ──────────────────────────────────────────────────

TEST(ConfigTest, DefaultValues) {
  EnvGuard port_guard("SERVER_PORT", "");
  EnvGuard host_guard("SERVER_HOST", "");
  EnvGuard db_host_guard("DB_HOST", "");
  EnvGuard db_user_guard("DB_USER", "");
  EnvGuard jwt_secret_guard("JWT_SECRET", "");
  EnvGuard log_dir_guard("LOG_DIR", "");
  EnvGuard static_dir_guard("STATIC_DIR", "");

  ServerConfig cfg = load_config();

  EXPECT_EQ(cfg.port, 8080);
  EXPECT_EQ(cfg.host, "0.0.0.0");
  EXPECT_EQ(cfg.db_host, "127.0.0.1");
  EXPECT_EQ(cfg.db_name, "oj_system");
  EXPECT_EQ(cfg.jwt_secret, "change-me-in-production");
  EXPECT_EQ(cfg.jwt_access_ttl, 900);
  EXPECT_EQ(cfg.jwt_refresh_ttl, 604800);
  EXPECT_EQ(cfg.seed_file, "data/seed.yaml");
  EXPECT_EQ(cfg.log_dir, "logs");
  EXPECT_EQ(cfg.static_dir, "static");
}

// ── 自定义环境变量测试 ──────────────────────────────────────────

TEST(ConfigTest, CustomEnvValues) {
  EnvGuard port_guard("SERVER_PORT", "9090");
  EnvGuard host_guard("SERVER_HOST", "127.0.0.1");
  EnvGuard db_name_guard("DB_NAME", "my_test_db");
  EnvGuard jwt_secret_guard("JWT_SECRET", "super-secret-key");
  EnvGuard log_dir_guard("LOG_DIR", "/tmp/logs");
  EnvGuard static_dir_guard("STATIC_DIR", "/var/www/static");

  ServerConfig cfg = load_config();

  EXPECT_EQ(cfg.port, 9090);
  EXPECT_EQ(cfg.host, "127.0.0.1");
  EXPECT_EQ(cfg.db_name, "my_test_db");
  EXPECT_EQ(cfg.jwt_secret, "super-secret-key");
  EXPECT_EQ(cfg.log_dir, "/tmp/logs");
  EXPECT_EQ(cfg.static_dir, "/var/www/static");
}

TEST(ConfigTest, JwtTtlEnvVariables) {
  EnvGuard access_guard("JWT_ACCESS_TTL", "1800");
  EnvGuard refresh_guard("JWT_REFRESH_TTL", "86400");

  ServerConfig cfg = load_config();

  EXPECT_EQ(cfg.jwt_access_ttl, 1800);
  EXPECT_EQ(cfg.jwt_refresh_ttl, 86400);
}

TEST(ConfigTest, DbPortAndPasswordEnvVariables) {
  EnvGuard port_guard("DB_PORT", "3307");
  EnvGuard pass_guard("DB_PASSWORD", "secret123");

  ServerConfig cfg = load_config();

  EXPECT_EQ(cfg.db_port, 3307);
  EXPECT_EQ(cfg.db_password, "secret123");
}

TEST(ConfigTest, SeedFileEnvVariable) {
  EnvGuard seed_guard("SEED_FILE", "/custom/path/seed.yaml");

  ServerConfig cfg = load_config();

  EXPECT_EQ(cfg.seed_file, "/custom/path/seed.yaml");
}

// ── 端口解析边界测试 ────────────────────────────────────────────

TEST(ConfigTest, PortParsesAsInteger) {
  EnvGuard port_guard("SERVER_PORT", "12345");

  ServerConfig cfg = load_config();

  EXPECT_EQ(cfg.port, 12345);
}

}  // namespace
}  // namespace vibeoj
