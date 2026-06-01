// JWT 令牌生成与验证 — HS256 签名的 access_token 和 refresh_token。
#pragma once

#include <string>
#include "model/user.h"

namespace vibeoj {

// 生成带自定义 TTL（秒）的 JWT，用于测试过期逻辑。
std::string generate_jwt(const User& user, const std::string& secret, int ttl);

// 生成 15 分钟过期的 access_token。
std::string generate_access_token(const User& user, const std::string& secret);

// 生成 7 天过期的 refresh_token。
std::string generate_refresh_token(const User& user, const std::string& secret);

// 验证 JWT，成功时填充 out_user 信息。返回 false 表示无效或过期。
bool verify_token(const std::string& token, const std::string& secret, User& out_user);

}  // namespace vibeoj
