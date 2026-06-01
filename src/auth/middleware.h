// 认证中间件 — Bearer token 提取、JWT 验证、管理员角色检查。
#pragma once

#include <string>
#include "model/user.h"

namespace vibeoj {
bool authenticate_request(const std::string& auth_header, const std::string& secret, User& out_user);
bool require_admin(const User& user);

}  // namespace vibeoj
