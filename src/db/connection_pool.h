// MySQL 连接池 — 管理可复用的数据库连接，线程安全。
// 使用方式：
//   auto guard = ConnectionPool::instance().acquire();
//   if (guard) {
//       auto stmt = guard->createStatement();
//       // ... use stmt ...
//   }  // guard 析构自动归还连接
#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

#include <cppconn/connection.h>

namespace vibeoj {

struct ServerConfig;

// RAII 连接守卫 — 析构时自动归还连接到池中。
class ConnectionGuard {
 public:
  ConnectionGuard() = default;
  ~ConnectionGuard();

  // 禁止拷贝，允许移动
  ConnectionGuard(const ConnectionGuard&) = delete;
  ConnectionGuard& operator=(const ConnectionGuard&) = delete;
  ConnectionGuard(ConnectionGuard&& other) noexcept;
  ConnectionGuard& operator=(ConnectionGuard&& other) noexcept;

  sql::Connection* operator->() const { return conn_.get(); }
  sql::Connection& operator*() const { return *conn_; }
  explicit operator bool() const { return conn_ != nullptr; }
  sql::Connection* get() const { return conn_.get(); }

 private:
  friend class ConnectionPool;
  ConnectionGuard(std::unique_ptr<sql::Connection> conn, class ConnectionPool* pool);

  std::unique_ptr<sql::Connection> conn_;
  ConnectionPool* pool_ = nullptr;  // 非 owning，用于归还连接
};

// 连接池单例 — 持有若干个数据库连接，按需分配。
class ConnectionPool {
 public:
  static ConnectionPool& instance();

  // 根据 ServerConfig 初始化连接池（仅首次调用有效）。
  void init(const ServerConfig& cfg);

  // 从池中获取一个连接，若池中有空闲则立即返回；
  // 若池未满且有空闲额度则创建新连接；
  // 若池已满则阻塞等待，超时 5 秒后返回空 guard。
  ConnectionGuard acquire();

  // 归还连接到池中（由 ConnectionGuard 析构时调用）。
  void release(std::unique_ptr<sql::Connection> conn);

  // 关闭所有连接，通常在进程退出前调用。
  void shutdown();

 private:
  ConnectionPool() = default;
  ~ConnectionPool();

  ConnectionPool(const ConnectionPool&) = delete;
  ConnectionPool& operator=(const ConnectionPool&) = delete;

  // 创建一个新连接，失败时返回 nullptr。
  std::unique_ptr<sql::Connection> create_connection();

  // 验证连接是否仍然有效。
  bool is_valid(sql::Connection* conn);

  std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<std::unique_ptr<sql::Connection>> pool_;

  // 连接参数
  std::string host_ = "127.0.0.1";
  int port_ = 3306;
  std::string user_ = "ljt";
  std::string password_ = "lijiatong344A@";
  std::string db_name_ = "oj_system";

  int pool_size_ = 8;     // 最大连接数
  int active_count_ = 0;  // 当前已创建的连接数（池中 + 借出）
  bool initialized_ = false;
};

}  // namespace vibeoj
