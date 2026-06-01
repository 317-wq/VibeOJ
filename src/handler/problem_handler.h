// 题目相关 HTTP 处理器 — GET /api/v1/problems（列表 + 含样例的详情）。
#pragma once

namespace httplib {
class Server;
}  // forward-declare cpp-httplib types

namespace vibeoj {

class ProblemHandler {
 public:
  // 注册题目相关路由到 httplib::Server。
  static void register_routes(httplib::Server& server);
};

}  // namespace vibeoj
