// 认证相关 HTTP 处理器 — POST /api/v1/auth/{register,login,logout,refresh}。
#pragma once

#include <string>

namespace httplib {
class Server;
}  // forward-declare cpp-httplib types

namespace vibeoj {

struct ServerConfig;

class AuthHandler {
 public:
  // 注册所有认证路由到 httplib::Server。
  static void register_routes(httplib::Server& server, const ServerConfig& cfg);
};

}  // namespace vibeoj
