// MySQL 连接池实现 — Phase 2。
#include "db/connection_pool.h"

namespace vibeoj {

ConnectionPool& ConnectionPool::instance() {
  static ConnectionPool pool;
  return pool;
}

}  // namespace vibeoj
