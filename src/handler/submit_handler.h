// 提交处理器 — POST /api/v1/submissions（提交代码）+ GET 查询结果/列表。
#pragma once

#include "httplib.h"
#include "config/config.h"

namespace vibeoj {

class SubmitHandler {
 public:
  static void register_routes(httplib::Server& server, const ServerConfig& cfg);
};

}  // namespace vibeoj
