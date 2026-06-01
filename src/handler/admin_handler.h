// 管理后台 HTTP 处理器 — 题目 CRUD、用户管理、系统统计（需 admin 角色）。
#pragma once

#include "httplib.h"
#include "config/config.h"

namespace vibeoj {

class AdminHandler {
 public:
  // 注册管理后台路由到 httplib::Server（所有端点需要 admin 权限）。
  static void register_routes(httplib::Server& server, const ServerConfig& cfg);
};

}  // namespace vibeoj
