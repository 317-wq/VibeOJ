// 认证中间件实现 — Phase 2。
#include "auth/middleware.h"

namespace vibeoj {

bool authenticate_request(const std::string&, const std::string&, User&) {
  return false;  // Phase 2
}

bool require_admin(const User& user) {
  return user.role == UserRole::admin;
}

}  // namespace vibeoj
