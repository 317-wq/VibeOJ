// RefreshToken 数据结构 — 持久化的刷新令牌，用于滑动会话刷新流程。
#pragma once

#include <cstdint>
#include <string>

namespace vibeoj {

struct RefreshToken {
  int64_t id;
  int64_t user_id;
  std::string token_hash;
  std::string expires_at;
  std::string created_at;
};

}  // namespace vibeoj
