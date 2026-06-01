// JWT 令牌生成与验证 — HS256 签名的 access_token 和 refresh_token。
#pragma once

#include <string>
#include "model/user.h"

namespace vibeoj {
std::string generate_access_token(const User& user, const std::string& secret);
std::string generate_refresh_token(const User& user, const std::string& secret);
bool verify_token(const std::string& token, const std::string& secret, User& out_user);

}  // namespace vibeoj
