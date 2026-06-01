// VibeOJ 服务入口 — 加载配置、初始化日志、启动 HTTP 服务器（含静态文件服务）。
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

// cpp-httplib header-only library
#include "httplib.h"

#include "common/log.h"
#include "config/config.h"
#include "config/seeder.h"
#include "db/connection_pool.h"
#include "handler/auth_handler.h"
#include "handler/problem_handler.h"

namespace {

// 全局 server 指针，用于信号处理函数中安全停止。
std::unique_ptr<httplib::Server> g_server;

// 信号处理函数 — 捕获 SIGINT / SIGTERM 后优雅关闭服务器。
void signal_handler(int signum) {
  LOG_INFO("Received signal %d, shutting down server...", signum);
  if (g_server) {
    g_server->stop();
  }
}

// 注册系统信号处理器。
void setup_signal_handlers() {
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);
}

// 通过可执行文件路径推断项目根目录。
// 典型场景：可执行文件在 build/ 下，项目根目录为 build/ 的父目录。
// 检测逻辑：若父目录不含 CMakeLists.txt 但上两级含，则取上两级。
// 若无法推断则返回当前工作目录。
std::string detect_project_root(const char* argv0) {
  try {
    std::filesystem::path exe_path = std::filesystem::canonical(argv0);
    std::filesystem::path dir = exe_path.parent_path();

    // 若当前目录包含 CMakeLists.txt 即是项目根目录
    if (std::filesystem::exists(dir / "CMakeLists.txt")) {
      return dir.string();
    }

    // 否则向上查找（处理 build/、cmake-build-*/ 等构建目录）
    std::filesystem::path parent = dir.parent_path();
    if (std::filesystem::exists(parent / "CMakeLists.txt")) {
      return parent.string();
    }

    // 若都找不到，返回可执行文件所在目录
    return dir.string();
  } catch (const std::filesystem::filesystem_error&) {
    return std::filesystem::current_path().string();
  }
}

// 将可能为相对路径的配置值解析为绝对路径。
// 规则：若 path 已是绝对路径则直接返回；
//       否则优先尝试项目根目录下的相对路径；
//       若项目根目录下不存在则尝试 CWD 下的相对路径；
//       都不存在则返回项目根目录下的路径（运行时可能创建，如 logs/）。
std::string resolve_path(const std::string& path,
                         const std::string& project_root) {
  if (path.empty()) return path;
  // 绝对路径直接返回
  if (path[0] == '/') return path;

  std::filesystem::path prj_root(project_root);

  // 优先：项目根目录下的相对路径
  std::filesystem::path prj_path = prj_root / path;
  if (std::filesystem::exists(prj_path)) {
    return std::filesystem::canonical(prj_path).string();
  }

  // 其次：CWD 下的相对路径
  std::filesystem::path cwd_path = std::filesystem::current_path() / path;
  if (std::filesystem::exists(cwd_path)) {
    return std::filesystem::canonical(cwd_path).string();
  }

  // 都不存在则返回项目根目录下的路径（运行时可能创建，如 logs/）
  return (prj_root / path).string();
}

// 注册所有 API 路由（各 handler 模块的完整实现在 Phase 2 中逐步添加）。
void register_routes(httplib::Server& server, const vibeoj::ServerConfig& cfg) {
  // ── 根路径 → 重定向到首页 ──────────────────────────────────
  server.Get("/", [](const httplib::Request&, httplib::Response& res) {
    res.set_redirect("/index.html");
  });

  // ── 健康检查 ──────────────────────────────────────────────
  server.Get("/api/v1/health", [](const httplib::Request&, httplib::Response& res) {
    res.set_content(R"({"status":"ok"})", "application/json");
  });

  // ── 认证路由 ────────────────────────────────────────────
  vibeoj::AuthHandler::register_routes(server, cfg);

  // ── 题目路由 ────────────────────────────────────────────
  vibeoj::ProblemHandler::register_routes(server);

  // ── 提交路由（stub） ─────────────────────────────────────
  server.Post("/api/v1/submissions", [](const httplib::Request& req, httplib::Response& res) {
    res.status = 501;
    res.set_content(R"({"error":"not implemented yet"})", "application/json");
  });

  // ── 管理后台路由（stub） ────────────────────────────────
  server.Post("/api/v1/admin/problems", [](const httplib::Request& req, httplib::Response& res) {
    res.status = 501;
    res.set_content(R"({"error":"not implemented yet"})", "application/json");
  });
}

}  // namespace

int main(int argc, char** argv) {
  // 1. 推断项目根目录
  std::string project_root = detect_project_root(argv[0]);
  std::cout << "Project root: " << project_root << std::endl;

  // 2. 加载配置
  vibeoj::ServerConfig cfg = vibeoj::load_config();

  // 3. 将相对路径解析为绝对路径（优先 CWD，回退到项目根目录）
  cfg.log_dir    = resolve_path(cfg.log_dir, project_root);
  cfg.static_dir = resolve_path(cfg.static_dir, project_root);
  cfg.seed_file  = resolve_path(cfg.seed_file, project_root);

  // 4. 初始化日志系统（使用绝对路径确保写入预期位置）
  vibeoj::Logger::instance().init(cfg.log_dir);
  LOG_INFO("VibeOJ Server starting on %s:%d...", cfg.host.c_str(), cfg.port);
  LOG_INFO("Project root: %s", project_root.c_str());
  LOG_INFO("Log directory: %s", cfg.log_dir.c_str());
  LOG_INFO("Static files: %s", cfg.static_dir.c_str());
  LOG_INFO("Database: %s@%s:%d/%s", cfg.db_user.c_str(), cfg.db_host.c_str(),
           cfg.db_port, cfg.db_name.c_str());

  // 5. 加载种子数据（解析失败不阻断启动，仅记录警告）
  try {
    auto data = vibeoj::parse_seed_file(cfg.seed_file);
    LOG_INFO("Seed file loaded: %d problems from %s",
             static_cast<int>(data.problems.size()), cfg.seed_file.c_str());
    for (const auto& p : data.problems) {
      LOG_DEBUG("  - [%s] %s (%d test cases)",
                vibeoj::difficulty_to_string(p.difficulty).c_str(), p.title.c_str(),
                static_cast<int>(p.test_cases.size()));
    }
  } catch (const std::exception& e) {
    LOG_WARNING("Failed to parse seed file '%s': %s — continuing without seed data",
                cfg.seed_file.c_str(), e.what());
  }

  // 6. 创建 HTTP 服务器实例
  g_server = std::make_unique<httplib::Server>();

  // 7. 注册信号处理器（优雅关闭）
  setup_signal_handlers();

  // 8. 挂载静态文件目录（前端 HTML/CSS/JS）
  //    注册的路由优先于静态文件，API 不会被覆盖。
  if (!cfg.static_dir.empty() && std::filesystem::exists(cfg.static_dir)) {
    auto ret = g_server->set_mount_point("/", cfg.static_dir.c_str());
    if (ret) {
      LOG_INFO("Static file serving enabled: %s -> /", cfg.static_dir.c_str());
    } else {
      LOG_WARNING("Failed to mount static directory: %s", cfg.static_dir.c_str());
    }
  } else {
    LOG_WARNING("Static directory not found: %s (browser will see 404 at /)",
                cfg.static_dir.c_str());
  }

  // 9. 初始化数据库连接池（在注册路由之前，因为 handler 需要连接池）
  vibeoj::ConnectionPool::instance().init(cfg);
  LOG_INFO("Database connection pool initialized");

  // 10. 注册 API 路由
  register_routes(*g_server, cfg);

  // 11. 启动监听
  LOG_INFO("HTTP server listening on http://%s:%d", cfg.host.c_str(), cfg.port);
  std::cout << "VibeOJ server running at http://" << cfg.host << ":" << cfg.port << std::endl;

  if (!g_server->listen(cfg.host.c_str(), cfg.port)) {
    LOG_FATAL("Failed to bind to %s:%d (port may be in use)", cfg.host.c_str(), cfg.port);
    std::cerr << "FATAL: Could not start HTTP server on " << cfg.host << ":" << cfg.port << std::endl;
    return 1;
  }

  LOG_INFO("Server stopped gracefully.");
  return 0;
}
