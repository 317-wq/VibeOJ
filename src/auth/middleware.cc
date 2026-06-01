// 认证中间件 — 从 Authorization header 提取 Bearer token 并验证 JWT。
#include "auth/middleware.h"

#include <string>

#include "auth/jwt.h"
#include "model/user.h"

namespace vibeoj {

bool authenticate_request(const std::string& auth_header,
                          const std::string& secret, User& out_user) {
  // 期望格式: "Bearer <token>"
  const std::string prefix = "Bearer ";
  if (auth_header.size() <= prefix.size() ||
      auth_header.compare(0, prefix.size(), prefix) != 0) {
    return false;
  }

  std::string token = auth_header.substr(prefix.size());
  return verify_token(token, secret, out_user);
}

bool require_admin(const User& user) {
  return user.role == UserRole::admin;
}

}  // namespace vibeoj
