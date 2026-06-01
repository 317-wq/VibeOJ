// JWT 令牌生成与验证 — HS256 (HMAC-SHA256)，基于 OpenSSL。
// token 格式: base64url(header).base64url(payload).base64url(signature)
#include "auth/jwt.h"

#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "nlohmann/json.hpp"
#include "model/user.h"

namespace vibeoj {

using json = nlohmann::json;

namespace {

// ── Base64URL 编解码 ──────────────────────────────────────────────

// 标准 base64 编码（无换行）。
std::string base64_encode(const unsigned char* data, size_t len) {
  // 预估编码后长度
  size_t out_len = 4 * ((len + 2) / 3);
  std::string out(out_len, '\0');

  int actual = EVP_EncodeBlock(
      reinterpret_cast<unsigned char*>(out.data()), data, static_cast<int>(len));
  // 去除尾部的 '=' padding 和可能的换行
  while (!out.empty() && (out.back() == '=' || out.back() == '\n')) {
    out.pop_back();
  }
  return out;
}

// base64 解码，返回解码后长度。buf 需足够大（至少 3 * (in_len/4) + 2）。
int base64_decode(const std::string& in, unsigned char* buf) {
  return EVP_DecodeBlock(buf,
      reinterpret_cast<const unsigned char*>(in.data()), static_cast<int>(in.size()));
}

// base64url: 将标准 base64 中的 '+' → '-', '/' → '_'。
std::string to_base64url(const std::string& b64) {
  std::string result = b64;
  for (char& c : result) {
    if (c == '+') c = '-';
    else if (c == '/') c = '_';
  }
  return result;
}

std::string from_base64url(const std::string& b64url) {
  std::string result = b64url;
  for (char& c : result) {
    if (c == '-') c = '+';
    else if (c == '_') c = '/';
  }
  // 补回 padding
  while (result.size() % 4 != 0) {
    result += '=';
  }
  return result;
}

// base64url 编码（兼容的便捷函数）。
std::string base64url_encode(const unsigned char* data, size_t len) {
  return to_base64url(base64_encode(data, len));
}

// ── HMAC-SHA256 ────────────────────────────────────────────────────

std::string hmac_sha256(const std::string& data, const std::string& key) {
  unsigned char result[EVP_MAX_MD_SIZE];
  unsigned int result_len = 0;

  HMAC(EVP_sha256(),
       key.data(), static_cast<int>(key.size()),
       reinterpret_cast<const unsigned char*>(data.data()), data.size(),
       result, &result_len);

  return std::string(reinterpret_cast<char*>(result), result_len);
}

// ── 时间格式化 ────────────────────────────────────────────────────

std::string format_db_time(time_t t) {
  struct tm tm_buf;
  gmtime_r(&t, &tm_buf);
  std::ostringstream oss;
  oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

}  // namespace

// ── 公开接口 ──────────────────────────────────────────────────────

std::string generate_jwt(const User& user, const std::string& secret, int ttl) {
  time_t now = std::time(nullptr);
  time_t exp = now + ttl;

  // Header
  json header = {{"alg", "HS256"}, {"typ", "JWT"}};
  std::string header_str = header.dump();
  std::string header_b64 = base64url_encode(
      reinterpret_cast<const unsigned char*>(header_str.data()), header_str.size());

  // Payload
  json payload = {
      {"sub", user.id},
      {"username", user.username},
      {"role", user_role_to_string(user.role)},
      {"iat", now},
      {"exp", exp}
  };
  std::string payload_str = payload.dump();
  std::string payload_b64 = base64url_encode(
      reinterpret_cast<const unsigned char*>(payload_str.data()), payload_str.size());

  // Signature
  std::string to_sign = header_b64 + "." + payload_b64;
  std::string sig = hmac_sha256(to_sign, secret);
  std::string sig_b64 = base64url_encode(
      reinterpret_cast<const unsigned char*>(sig.data()), sig.size());

  return to_sign + "." + sig_b64;
}

std::string generate_access_token(const User& user, const std::string& secret) {
  return generate_jwt(user, secret, 900);  // 15 min
}

std::string generate_refresh_token(const User& user, const std::string& secret) {
  return generate_jwt(user, secret, 604800);  // 7 days
}

bool verify_token(const std::string& token, const std::string& secret, User& out_user) {
  // 分割 token: header.payload.signature
  auto dot1 = token.find('.');
  if (dot1 == std::string::npos) return false;
  auto dot2 = token.find('.', dot1 + 1);
  if (dot2 == std::string::npos) return false;

  std::string header_b64  = token.substr(0, dot1);
  std::string payload_b64 = token.substr(dot1 + 1, dot2 - dot1 - 1);
  std::string sig_b64     = token.substr(dot2 + 1);

  // 验证签名
  std::string to_sign = header_b64 + "." + payload_b64;
  std::string expected_sig_raw = hmac_sha256(to_sign, secret);
  std::string expected_sig = base64url_encode(
      reinterpret_cast<const unsigned char*>(expected_sig_raw.data()),
      expected_sig_raw.size());

  if (sig_b64 != expected_sig) return false;

  // 解码 payload
  std::string payload_b64std = from_base64url(payload_b64);
  std::vector<unsigned char> buf(payload_b64std.size());
  int decoded_len = base64_decode(payload_b64std, buf.data());
  if (decoded_len <= 0) return false;
  std::string payload_json(reinterpret_cast<const char*>(buf.data()), decoded_len);

  // 解析 JSON payload
  try {
    json j = json::parse(payload_json);

    // 检查过期
    int64_t exp = j.value("exp", int64_t(0));
    time_t now = std::time(nullptr);
    if (exp <= now) return false;

    // 填充 User 信息
    out_user.id       = j.value("sub", int64_t(0));
    out_user.username = j.value("username", std::string{});
    out_user.role     = user_role_from_string(j.value("role", std::string{"user"}));
  } catch (...) {
    return false;
  }

  return true;
}

}  // namespace vibeoj
