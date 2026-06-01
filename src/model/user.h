// User 数据结构 — 角色与状态枚举，用于认证和管理后台。
#pragma once

#include <cstdint>
#include <string>

namespace vibeoj {

enum class UserRole {
  user,
  admin
};

inline std::string user_role_to_string(UserRole r) {
  return r == UserRole::admin ? "admin" : "user";
}

inline UserRole user_role_from_string(const std::string& s) {
  return s == "admin" ? UserRole::admin : UserRole::user;
}

enum class UserStatus {
  active,
  disabled
};

inline std::string user_status_to_string(UserStatus s) {
  return s == UserStatus::disabled ? "disabled" : "active";
}

inline UserStatus user_status_from_string(const std::string& s) {
  return s == "disabled" ? UserStatus::disabled : UserStatus::active;
}

struct User {
  int64_t id;
  std::string username;
  std::string password_hash;
  UserRole role;
  UserStatus status;
  std::string created_at;
};

}  // namespace vibeoj
