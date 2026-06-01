// JWT 实现（HMAC-SHA256）— Phase 2。
#include "auth/jwt.h"

namespace vibeoj {

std::string generate_access_token(const User&, const std::string&) {
  return "";  // Phase 2
}

std::string generate_refresh_token(const User&, const std::string&) {
  return "";  // Phase 2
}

bool verify_token(const std::string&, const std::string&, User&) {
  return false;  // Phase 2
}

}  // namespace vibeoj
