// 配置加载模块 — 从环境变量读取运行参数（端口、DB连接、JWT密钥等）。
#pragma once

#include <string>

namespace vibeoj {

// ServerConfig 保存服务器运行所需的所有配置参数。
// 每个字段都有合理的默认值，可通过环境变量覆盖。
struct ServerConfig {
  // HTTP 服务监听端口，默认 8080
  int port = 8080;
  // 监听地址，默认 0.0.0.0（所有接口）
  std::string host = "0.0.0.0";
  // MySQL 主机地址
  std::string db_host = "127.0.0.1";
  // MySQL 端口
  int db_port = 3306;
  // MySQL 用户名（auth_socket 插件下可与系统用户同名免密）
  std::string db_user = "ljt";
  // MySQL 密码（auth_socket 免密时为空）
  std::string db_password;
  // MySQL 数据库名
  std::string db_name = "oj_system";
  // JWT 签名密钥（生产环境务必通过环境变量设置）
  std::string jwt_secret = "change-me-in-production";
  // JWT Access Token 过期时间（秒），默认 15 分钟
  int jwt_access_ttl = 900;
  // JWT Refresh Token 过期时间（秒），默认 7 天
  int jwt_refresh_ttl = 604800;
  // 种子数据文件路径
  std::string seed_file = "data/seed.yaml";
  // 日志目录
  std::string log_dir = "logs";
  // 静态文件目录（前端 HTML/CSS/JS）
  std::string static_dir = "static";
};

// 从环境变量加载 ServerConfig，未设置的变量使用默认值。
// 支持的变量：SERVER_PORT, SERVER_HOST, DB_HOST, DB_PORT, DB_USER,
//             DB_PASSWORD, DB_NAME, JWT_SECRET, JWT_ACCESS_TTL,
//             JWT_REFRESH_TTL, SEED_FILE, LOG_DIR
ServerConfig load_config();

}  // namespace vibeoj
