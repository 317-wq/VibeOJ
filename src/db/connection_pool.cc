// MySQL 连接池实现 — 线程安全的连接复用与 RAII 归还。
#include "db/connection_pool.h"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>

#include <cppconn/connection.h>
#include <cppconn/exception.h>
#include <cppconn/driver.h>
#include <cppconn/statement.h>
#include <mysql_driver.h>
#include <mysql_connection.h>

#include "common/log.h"
#include "config/config.h"

namespace vibeoj {

// ── ConnectionGuard ─────────────────────────────────────────────

ConnectionGuard::ConnectionGuard(std::unique_ptr<sql::Connection> conn,
                                 ConnectionPool* pool)
    : conn_(std::move(conn)), pool_(pool) {}

ConnectionGuard::~ConnectionGuard() {
  if (conn_ && pool_) {
    pool_->release(std::move(conn_));
  }
}

ConnectionGuard::ConnectionGuard(ConnectionGuard&& other) noexcept
    : conn_(std::move(other.conn_)), pool_(other.pool_) {
  other.pool_ = nullptr;
}

ConnectionGuard& ConnectionGuard::operator=(ConnectionGuard&& other) noexcept {
  if (this != &other) {
    // 先归还当前持有的连接
    if (conn_ && pool_) {
      pool_->release(std::move(conn_));
    }
    conn_ = std::move(other.conn_);
    pool_ = other.pool_;
    other.pool_ = nullptr;
  }
  return *this;
}

// ── ConnectionPool ──────────────────────────────────────────────

ConnectionPool& ConnectionPool::instance() {
  static ConnectionPool pool;
  return pool;
}

ConnectionPool::~ConnectionPool() {
  shutdown();
}

void ConnectionPool::init(const ServerConfig& cfg) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (initialized_) return;

  host_     = cfg.db_host;
  port_     = cfg.db_port;
  user_     = cfg.db_user;
  password_ = cfg.db_password;
  db_name_  = cfg.db_name;

  // 连接池大小从环境变量读取，默认与硬件线程数相同但不超过 16
  const char* env_pool_size = std::getenv("DB_POOL_SIZE");
  if (env_pool_size && env_pool_size[0]) {
    pool_size_ = std::stoi(env_pool_size);
  } else {
    unsigned int hw = std::thread::hardware_concurrency();
    pool_size_ = std::max(4, std::min(static_cast<int>(hw), 16));
  }

  initialized_ = true;
  LOG_INFO("ConnectionPool initialized: %s:%d/%s, pool_size=%d, user=%s",
           host_.c_str(), port_, db_name_.c_str(), pool_size_, user_.c_str());
}

std::unique_ptr<sql::Connection> ConnectionPool::create_connection() {
  try {
    sql::mysql::MySQL_Driver* driver =
        sql::mysql::get_mysql_driver_instance();

    std::string host_port =
        host_ + ":" + std::to_string(port_);
    auto* conn = driver->connect(host_port, user_, password_);

    conn->setSchema(db_name_);
    // 使用 utf8mb4 字符集以支持中文和 emoji
    {
      auto stmt = conn->createStatement();
      stmt->execute("SET NAMES utf8mb4");
      delete stmt;
    }

    return std::unique_ptr<sql::Connection>(conn);
  } catch (const sql::SQLException& e) {
    LOG_ERROR("Failed to create MySQL connection: %s (code=%d, state=%s)",
              e.what(), e.getErrorCode(), e.getSQLState().c_str());
    return nullptr;
  }
}

bool ConnectionPool::is_valid(sql::Connection* conn) {
  if (!conn || conn->isClosed()) return false;
  try {
    // 轻量探测，避免发送真实查询
    return conn->isValid();
  } catch (...) {
    return false;
  }
}

ConnectionGuard ConnectionPool::acquire() {
  std::unique_lock<std::mutex> lock(mutex_);

  if (!initialized_) {
    LOG_ERROR("ConnectionPool::acquire() called before init()");
    return {};
  }

  // 等待直到有空闲连接或可以创建新连接
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

  while (pool_.empty() && active_count_ >= pool_size_) {
    if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
      LOG_WARNING("ConnectionPool: acquire timeout, all %d connections busy",
                  pool_size_);
      return {};
    }
  }

  // 优先复用池中已有连接
  if (!pool_.empty()) {
    auto conn = std::move(pool_.front());
    pool_.pop();

    // 验证连接是否仍然有效，无效则创建新连接替代
    if (!is_valid(conn.get())) {
      LOG_WARNING("ConnectionPool: stale connection detected, replacing");
      conn.reset();
      active_count_--;
    } else {
      return ConnectionGuard(std::move(conn), this);
    }
  }

  // 创建新连接（active_count_ 可能已因失效连接减 1，所以再次判断）
  if (active_count_ < pool_size_) {
    auto conn = create_connection();
    if (conn) {
      active_count_++;
      return ConnectionGuard(std::move(conn), this);
    }
  }

  // 创建失败且池中无可用连接
  LOG_ERROR("ConnectionPool: cannot provide connection");
  return {};
}

void ConnectionPool::release(std::unique_ptr<sql::Connection> conn) {
  if (!conn) return;

  std::lock_guard<std::mutex> lock(mutex_);
  if (is_valid(conn.get())) {
    pool_.push(std::move(conn));
    cv_.notify_one();
  } else {
    // 连接已失效，直接丢弃
    conn.reset();
    active_count_--;
    LOG_WARNING("ConnectionPool: released a broken connection, discarded");
  }
}

void ConnectionPool::shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);
  while (!pool_.empty()) {
    auto& conn = pool_.front();
    try {
      if (conn && !conn->isClosed()) {
        conn->close();
      }
    } catch (...) {
      // 忽略关闭时的异常
    }
    pool_.pop();
    active_count_--;
  }
  initialized_ = false;
}

}  // namespace vibeoj
