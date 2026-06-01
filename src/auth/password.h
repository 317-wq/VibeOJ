// 密码哈希 — 基于 OpenSSL 的 bcrypt 哈希与验证。
#pragma once

#include <string>

namespace vibeoj {
std::string hash_password(const std::string& password);
bool verify_password(const std::string& password, const std::string& hash);

}  // namespace vibeoj
