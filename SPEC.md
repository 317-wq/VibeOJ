# VibeOJ — 仿 LeetCode 在线判题系统 · 规格说明书

---

## 1. 项目概述

**VibeOJ** 是一个基于 C++ 后端的在线判题系统（Online Judge），用于个人技术作品展示。支持 C++ 代码提交、自动编译判题、结果反馈，并提供用户认证和管理后台。

| 属性 | 决策 |
|------|------|
| 定位 | 个人项目/作品集 |
| 判题语言 | 仅 C++ |
| 判题沙箱 | 进程级隔离 (fork + ulimit) |
| 用户认证 | 注册/登录，JWT Access Token (header) + Refresh Token (httpOnly cookie) |
| 数据存储 | MySQL |
| 部署方式 | Docker Compose 一键部署 |
| 构建系统 | CMake |

---

## 2. 架构总览

```
┌──────────────────────────────────────────────────────────┐
│                       Nginx (port 80)                     │
│           静态文件服务 + API 反向代理 (:8080)              │
└──────────┬───────────────────────────────┬───────────────┘
           │                               │
    ┌──────▼──────┐                 ┌──────▼──────┐
    │  前端 (MPA)  │                 │  后端 (C++)  │
    │ HTML+CSS+JS │  RESTful JSON   │ cpp-httplib  │
    │  6 个页面   │◄────────────────│   :8080      │
    └─────────────┘                 └──────┬───────┘
                                           │
                          ┌────────────────┼────────────────┐
                          │                │                │
                   ┌──────▼──────┐  ┌──────▼──────┐  ┌─────▼─────┐
                   │  MySQL 容器  │  │  判题引擎    │  │ 文件系统   │
                   │   :3306     │  │ fork+ulimit  │  │ /tmp/judge │
                   └─────────────┘  └──────────────┘  └───────────┘
```

### 后端模块划分

```
src/
├── main.cc              # 入口，启动 HTTP server
├── common/              # 通用工具模块
│   └── log              # 日志记录 (分级输出 → logs/ 目录 + stderr)
├── config/              # 配置加载 (端口、DB连接、JWT密钥等)
├── db/                  # MySQL 连接池 & DAO 层
├── auth/                # JWT 生成/验证中间件，密码 bcrypt 哈希
├── handler/             # HTTP 请求处理器 (按资源拆分)
│   ├── auth_handler     # 注册、登录、登出、refresh
│   ├── problem_handler  # 题目 CRUD (admin)、题目列表/详情 (user)
│   ├── submit_handler   # 代码提交、判题结果查询
│   └── admin_handler    # 用户管理、统计数据
├── judge/               # 判题引擎
│   ├── compiler         # g++ 编译
│   ├── sandbox          # fork + ulimit + 管道通信
│   └── runner           # 线程池调度
└── model/               # 数据结构定义 (Problem, User, Submission 等)
```

### 前端页面结构

```
static/
├── index.html           # 首页 — 题目列表
├── login.html           # 登录页
├── register.html        # 注册页
├── problem.html         # 做题页 — 题目描述 + 代码编辑(textarea) + 判题结果
├── submissions.html     # 提交记录页 — 个人历史提交列表
├── admin.html           # 管理后台 — 题目CRUD + 用户管理 + 统计
├── css/
│   └── style.css        # 全局样式
└── js/
    ├── api.js           # AJAX 封装、token 管理、自动 refresh
    └── utils.js         # 通用工具函数
```

---

## 3. 技术栈

| 层 | 选型 | 理由 |
|----|------|------|
| 后端 HTTP | cpp-httplib | header-only，轻量，C++17 |
| 数据库 | MySQL 8.0 | C++ 生态成熟，官方 mysql-connector-cpp |
| 数据库连接池 | 自研简单池 / mysql-connector-cpp 内置 | 避免引入重型 ORM |
| 密码哈希 | bcrypt (libbcrypt / OpenSSL) | 行业标准 |
| JWT | 自研 HMAC-SHA256 或 jwt-cpp | 轻量，避免引入过多依赖 |
| 前端 | 原生 HTML + CSS + JS | 零框架/零构建，直接可用 |
| 容器编排 | Docker Compose (3 服务: nginx, app, mysql) | 一键部署 |
| 构建 | CMake 3.16+ | 现代 C++ 标准 |
| 编译器 | g++ (容器内预装) | 判题编译用 |

---

## 4. 数据库设计

### 4.1 ER 概要

```
users 1───N submissions N───1 problems
                         1───N test_cases
users 1───N refresh_tokens
```

### 4.2 表结构

**users**

| 列 | 类型 | 说明 |
|----|------|------|
| id | BIGINT PK AUTO_INCREMENT | |
| username | VARCHAR(64) UNIQUE NOT NULL | |
| password_hash | VARCHAR(256) NOT NULL | bcrypt |
| role | ENUM('user','admin') DEFAULT 'user' | |
| status | ENUM('active','disabled') DEFAULT 'active' | 账号状态，管理员可禁用 |
| created_at | DATETIME DEFAULT CURRENT_TIMESTAMP | |

**problems**

| 列 | 类型 | 说明 |
|----|------|------|
| id | BIGINT PK AUTO_INCREMENT | |
| title | VARCHAR(256) NOT NULL | |
| description | TEXT NOT NULL | Markdown 存储 |
| difficulty | ENUM('easy','medium','hard') NOT NULL | |
| time_limit_ms | INT DEFAULT 1000 | |
| memory_limit_kb | INT DEFAULT 262144 | 256MB |
| created_by | BIGINT FK→users.id | |
| created_at | DATETIME DEFAULT CURRENT_TIMESTAMP | |

**test_cases**

| 列 | 类型 | 说明 |
|----|------|------|
| id | BIGINT PK AUTO_INCREMENT | |
| problem_id | BIGINT FK→problems.id | |
| input | TEXT NOT NULL | stdin 输入 |
| expected_output | TEXT NOT NULL | 期望 stdout |
| is_sample | BOOLEAN DEFAULT FALSE | 是否对用户可见的样例 |
| order_index | INT | 用例执行顺序 |

**submissions**

| 列 | 类型 | 说明 |
|----|------|------|
| id | BIGINT PK AUTO_INCREMENT | |
| user_id | BIGINT FK→users.id | |
| problem_id | BIGINT FK→problems.id | |
| code | MEDIUMTEXT NOT NULL | |
| status | ENUM('pending','compiling','running','accepted','wrong_answer','time_limit','memory_limit','runtime_error','compile_error','system_error') | system_error 表示非用户代码导致的系统异常 |
| compile_output | TEXT | 编译错误信息 |
| passed_cases | INT DEFAULT 0 | |
| total_cases | INT DEFAULT 0 | |
| time_used_ms | INT | 最大耗时 |
| memory_used_kb | INT | 最大内存 |
| diff_output | TEXT | WA 时的 diff 信息 |
| created_at | DATETIME DEFAULT CURRENT_TIMESTAMP | |
| updated_at | DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP | 判题完成/状态变更时间 |

**refresh_tokens**

| 列 | 类型 | 说明 |
|----|------|------|
| id | BIGINT PK AUTO_INCREMENT | |
| user_id | BIGINT FK→users.id | |
| token_hash | VARCHAR(256) UNIQUE NOT NULL | SHA256(token) |
| expires_at | DATETIME NOT NULL | |
| created_at | DATETIME DEFAULT CURRENT_TIMESTAMP | |

### 4.3 连接方式

本地 MySQL 使用 `auth_socket` 插件认证，与 Linux 系统用户同名即可免密登录：

```bash
# 本地免密登录（当前用户 ljt）
mysql -uljt

# 指定数据库
mysql -uljt oj_system

# 执行 SQL 脚本
mysql -uljt oj_system < scripts/init.sql
```

| 配置项 | 值 |
|--------|-----|
| 数据库名 | `oj_system` |
| 本地用户 | `ljt`（auth_socket 免密） |
| 远程用户 | `ljt`（密码 `*****`） |
| root 登录 | `sudo mysql`（auth_socket） |

---

## 5. API 设计

Base URL: `/api/v1`

### 5.1 认证

| 方法 | 路径 | 说明 | Auth |
|------|------|------|------|
| POST | /auth/register | 注册 | - |
| POST | /auth/login | 登录，返回 access_token + Set-Cookie refresh_token | - |
| POST | /auth/refresh | 用 cookie 中的 refresh_token 换新 access_token | Cookie |
| POST | /auth/logout | 撤销 refresh_token | Cookie |

### 5.2 题目

| 方法 | 路径 | 说明 | Auth |
|------|------|------|------|
| GET | /problems | 题目列表 (支持 ?difficulty=&page=&size=) | 可选 |
| GET | /problems/:id | 题目详情 + 可见样例 | 可选 |

### 5.3 提交与判题

| 方法 | 路径 | 说明 | Auth |
|------|------|------|------|
| POST | /submissions | 提交代码 {problem_id, code} → 返回 submission_id | Bearer |
| GET | /submissions/:id | 查询判题结果 | Bearer |
| GET | /submissions?problem_id=&user_id= | 提交列表 | Bearer |

### 5.4 管理后台 (admin only)

| 方法 | 路径 | 说明 | Auth |
|------|------|------|------|
| POST | /admin/problems | 创建题目 | Bearer+Admin |
| PUT | /admin/problems/:id | 编辑题目 | Bearer+Admin |
| DELETE | /admin/problems/:id | 删除题目 | Bearer+Admin |
| POST | /admin/problems/:id/testcases | 添加测试用例 | Bearer+Admin |
| PUT | /admin/testcases/:id | 编辑测试用例 | Bearer+Admin |
| DELETE | /admin/testcases/:id | 删除测试用例 | Bearer+Admin |
| GET | /admin/users | 用户列表 | Bearer+Admin |
| PUT | /admin/users/:id | 修改用户角色/禁用 | Bearer+Admin |
| GET | /admin/stats | 统计数据 (提交量/通过率趋势) | Bearer+Admin |

### 5.5 认证流程

```
登录成功 → 响应体: { access_token (15min) }
         → Set-Cookie: refresh_token (httpOnly, Secure, SameSite=Strict, 7d)

API 请求 → Authorization: Bearer <access_token>
         → 401 → 前端自动 POST /auth/refresh (cookie 自动携带)
              → 成功 → 更新 access_token，重放原请求
              → 失败 → 跳转登录页
```

---

## 6. 判题流程

```
用户提交代码
     │
     ▼
┌──────────────┐    compilation     ┌─────────────────┐
│ 创建 Submission │────────────────►│ 写入 /tmp/judge/ │
│ status=pending  │                 │ <id>.cpp         │
└──────────────┘                   └───────┬───────────┘
                                           │
                                    g++ -std=c++17 -O2
                                           │
                              ┌────────────┼────────────┐
                              ▼                         ▼
                        编译成功                  编译失败
                              │                  status=compile_error
                              │                  compile_output=err
                              ▼
                    ┌──────────────────┐
                    │ 逐个运行测试用例    │
                    │ fork() 子进程执行  │
                    │ ./a.out < in.txt  │
                    └──────┬───────────┘
                           │
              ┌────────────┼────────────┐
              ▼            ▼            ▼
         全部 AC      WA/TLE/MLE    RTE
                           │
                           ▼
                    ┌──────────────┐
                    │ diff 预期输出  │
                    │ 与 实际输出    │
                    └──────────────┘
```

### 沙箱约束 (fork + ulimit)

```c
// 子进程限制
setrlimit(RLIMIT_CPU,    {time_limit_sec, time_limit_sec + 1}); // 软/硬 CPU 时间
setrlimit(RLIMIT_AS,     {memory_limit_bytes, memory_limit_bytes});
setrlimit(RLIMIT_NPROC,  {0, 0});   // 禁止 fork
setrlimit(RLIMIT_FSIZE,  {0, 0});   // 禁止写文件

// stdin/stdout 通过管道重定向
dup2(pipe_stdin[0],  STDIN_FILENO);
dup2(pipe_stdout[1], STDOUT_FILENO);

execve("./a.out", ...);
```

---

## 7. 判题线程池

```
┌──────────────────────────────┐
│      Judge Thread Pool       │
│   ┌───┐ ┌───┐ ┌───┐ ┌───┐   │
│   │ W │ │ W │ │ W │ │ W │   │  ← N = std::thread::hardware_concurrency()
│   └───┘ └───┘ └───┘ └───┘   │
│        ▲                     │
│        │ 任务入队              │
│   ┌─────────┐                │
│   │ pending │  ← submission_id│
│   │  queue  │                │
│   └─────────┘                │
└──────────────────────────────┘
```

- 使用 `std::queue` + `std::mutex` + `std::condition_variable`
- Submission 入队后立即返回 `submission_id`，前端轮询结果

---

## 8. 数据初始化

题目和测试用例通过 YAML 配置文件在数据库首次启动时导入：

```yaml
# data/seed.yaml
problems:
  - title: "两数之和"
    description: "给定一个整数数组 nums 和一个整数目标值 target..."
    difficulty: easy
    time_limit_ms: 1000
    memory_limit_kb: 262144
    test_cases:
      - input: "3\n2 7 11 15\n9\n"
        expected_output: "0 1\n"
        is_sample: true
      - input: "2\n3 3\n6\n"
        expected_output: "0 1\n"
        is_sample: false
  # ... more problems
```

后端启动时检查数据库是否已有题目数据，若为空则从 `data/seed.yaml` 导入。

---

## 9. Docker Compose 拓扑

```yaml
services:
  nginx:
    image: nginx:alpine
    ports: ["80:80"]
    volumes: ["./static:/usr/share/nginx/html", "./nginx.conf:/etc/nginx/nginx.conf"]

  app:
    build: .
    environment: ["DB_HOST=mysql", "JWT_SECRET=...", "SEED_FILE=/data/seed.yaml"]
    volumes: ["./data:/data"]
    depends_on: [mysql]

  mysql:
    image: mysql:8.0
    environment: ["MYSQL_ROOT_PASSWORD=...", "MYSQL_DATABASE=vibeoj"]
    volumes: ["mysql_data:/var/lib/mysql"]
    ports: ["3306:3306"]  # 仅开发调试用

volumes:
  mysql_data:
```

---

## 10. 测试策略

| 范围 | 工具 | 覆盖目标 |
|------|------|----------|
| 日志模块 | C++ 单测 (Google Test) | 日志等级/时间戳/文件行号/线程安全/长消息 (12 tests) |
| 种子数据 | C++ 单测 (Google Test) | YAML 解析/默认值/多题目 (10 tests) |
| 判题沙箱 | C++ 单测 (Google Test / Catch2) | fork/ulimit/管道通信正确性 |
| 认证模块 | C++ 单测 | JWT 签发/验证/过期逻辑 |
| API 集成 | curl 脚本 / Postman | 端到端流程 |

---

## 11. TODO 清单

### Phase 1: 项目骨架 ✅
- [x] CMakeLists.txt 项目结构搭建 (含 GTest 单元测试框架)
- [x] Dockerfile + docker-compose.yml (含 MySQL .so 版本匹配修复)
- [x] nginx.conf 反向代理配置 (含 gzip + 静态资源缓存)
- [x] MySQL 初始化 SQL 脚本 (含性能索引 + 远程用户)
- [x] 数据种子文件 `data/seed.yaml`
- [x] .gitignore / .dockerignore
- [x] 模块头文件定义 (model/, config/, db/, auth/, handler/, judge/)
- [x] 种子数据解析器实现 + 单元测试 (10 tests passed)

### Phase 2: 后端核心
- [x] 日志记录模块 (common/log) — 分级日志输出到 logs/ 目录 + stderr，线程安全
- [x] cpp-httplib HTTP server 启动
- [x] MySQL 连接池实现 — 含 DAO 层 (UserDAO/ProblemDAO/TestCaseDAO/SubmissionDAO/RefreshTokenDAO)，31 个单元测试
- [ ] 用户注册/登录 API (bcrypt + JWT)
- [ ] JWT 中间件 (access_token 验证 + refresh 流程)
- [ ] 题目列表/详情 API
- [ ] 代码提交 API (接收代码 → 写入 submission → 入队)
- [ ] 判题引擎 (g++ 编译 + fork/ulimit 执行 + diff)
- [ ] 判题线程池
- [ ] 提交记录查询 API
- [ ] 管理后台 API (题目CRUD + 用户管理 + 统计)
- [ ] 种子数据自动导入逻辑

### Phase 3: 前端页面
- [ ] 全局样式 `css/style.css`
- [ ] `js/api.js` (fetch 封装 + token 管理 + 自动 refresh)
- [ ] `js/utils.js` (通用工具)
- [ ] `index.html` — 首页题目列表
- [ ] `login.html` — 登录页
- [ ] `register.html` — 注册页
- [ ] `problem.html` — 做题页 (textarea 编辑 + 提交 + 结果)
- [ ] `submissions.html` — 提交记录页
- [ ] `admin.html` — 管理后台

### Phase 4: 测试与收尾
- [ ] 判题沙箱单元测试
- [ ] 认证模块单元测试
- [ ] API 端到端验证脚本
- [ ] README.md (项目说明 + 部署步骤)
- [ ] Docker Compose 全链路验证

---

## 12. 验收标准

| # | 标准 | 验证方式 |
|---|------|----------|
| 1 | 用户可注册、登录、登出，token 过期可自动 refresh | 手动测试 + curl |
| 2 | 首页展示题目列表，支持难度筛选 | 浏览器验证 |
| 3 | 做题页可查看题目描述+样例，textarea 编写代码并提交 | 浏览器验证 |
| 4 | C++ 代码提交后自动编译，编译错误返回完整 g++ 输出 | 提交错误代码验证 |
| 5 | 判题结果正确返回 AC / WA(含 diff) / TLE / MLE / RTE | 提交正确/错误代码验证 |
| 6 | 提交记录页可查看历史，点击可展开详情 | 浏览器验证 |
| 7 | 管理员可创建/编辑/删除题目和测试用例 | 登录 admin 验证 |
| 8 | 管理员可查看用户列表和统计数据 | 登录 admin 验证 |
| 9 | `docker compose up` 一键启动全部服务 | 干净环境验证 |
| 10 | 判题沙箱在子进程超内存/超时后正确 kill 并回收 | 提交死循环/大内存代码验证 |

---

> 本规格基于 2026-06-01 深度访谈生成，共 6 轮 24 个问题。

---

## 13. 开发环境配置

### 13.1 IDE 红色波浪线修复（compile_commands.json）

CMake FetchContent 拉取的依赖（yaml-cpp、nlohmann/json、cpp-httplib）头文件路径在 `build/_deps/` 下，IDE 的 clangd/C++ 插件无法自动发现这些路径，导致 `#include "yaml-cpp/yaml.h"` 等语句显示红色错误下划线。

**解决方案**：

CMakeLists.txt 已配置 `CMAKE_EXPORT_COMPILE_COMMANDS ON`，构建后会在 `build/` 目录生成 `compile_commands.json`，项目根目录有符号链接指向它：

```bash
# 初次配置后执行
cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)

# 确认符号链接存在
ls -l compile_commands.json  # → build/compile_commands.json
```

重新加载 IDE 窗口后，clangd 读取 `compile_commands.json` 即可解析所有 include 路径，红色波浪线消失。

> **注意**：每次 CMake 配置参数变更（如新增源文件、修改 FetchContent 依赖）后需要重新构建以更新 `compile_commands.json`。该文件已加入 `.gitignore`。
