// 配置加载模块实现 — 从环境变量读取各项运行参数。
#include "config/config.h"

#include <cstdlib>
#include <string>

namespace vibeoj {

namespace {

// 读取环境变量，若未设置则返回默认值。
inline int env_int(const char* name, int default_val) {
  const char* val = std::getenv(name);
  if (val && val[0] != '\0') {
    return std::stoi(val);
  }
  return default_val;
}

inline std::string env_str(const char* name, const std::string& default_val) {
  const char* val = std::getenv(name);
  if (val && val[0] != '\0') {
    return std::string(val);
  }
  return default_val;
}

}  // namespace

ServerConfig load_config() {
  ServerConfig cfg;
  cfg.port           = env_int("SERVER_PORT", 8080);
  cfg.host           = env_str("SERVER_HOST", "0.0.0.0");
  cfg.db_host        = env_str("DB_HOST", "127.0.0.1");
  cfg.db_port        = env_int("DB_PORT", 3306);
  cfg.db_user        = env_str("DB_USER", "ljt");
  cfg.db_password    = env_str("DB_PASSWORD", "");
  cfg.db_name        = env_str("DB_NAME", "oj_system");
  cfg.jwt_secret     = env_str("JWT_SECRET", "change-me-in-production");
  cfg.jwt_access_ttl = env_int("JWT_ACCESS_TTL", 900);
  cfg.jwt_refresh_ttl= env_int("JWT_REFRESH_TTL", 604800);
  cfg.seed_file      = env_str("SEED_FILE", "data/seed.yaml");
  cfg.log_dir        = env_str("LOG_DIR", "logs");
  cfg.static_dir     = env_str("STATIC_DIR", "static");
  return cfg;
}

}  // namespace vibeoj
