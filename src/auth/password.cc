// bcrypt 密码哈希 — 基于 libcrypt 的 crypt_r / crypt_gensalt。
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "auth/password.h"

#include <crypt.h>
#include <cstring>

namespace vibeoj {

// bcrypt cost factor: 2^10 = 1024 iterations
constexpr int kCostFactor = 10;

std::string hash_password(const std::string& password) {
  const char* salt = crypt_gensalt("$2b$", kCostFactor, nullptr, 0);
  if (!salt) return "";

  struct crypt_data data;
  std::memset(&data, 0, sizeof(data));
  const char* hash = crypt_r(password.c_str(), salt, &data);
  if (!hash) return "";

  return std::string(hash);
}

bool verify_password(const std::string& password, const std::string& hash) {
  struct crypt_data data;
  std::memset(&data, 0, sizeof(data));
  const char* result = crypt_r(password.c_str(), hash.c_str(), &data);
  if (!result) return false;

  return std::strcmp(result, hash.c_str()) == 0;
}

}  // namespace vibeoj
