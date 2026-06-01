# VibeOJ — 环境依赖与配置指南

> 适用系统：Ubuntu 24.04 (Noble Numbal) 全新安装
> 最后更新：2026-06-01

---

## 1. 依赖总览

```
┌─────────────────────────────────────────────────┐
│              宿主机构建 / 开发依赖                │
├────────────────────┬────────────────────────────┤
│ build-essential    │ gcc, g++, make             │
│ cmake (≥3.16)      │ 构建系统                    │
│ git                │ CMake FetchContent 拉取依赖 │
│ libssl-dev         │ OpenSSL 开发头 (JWT/bcrypt) │
│ libmysqlcppconn-dev│ MySQL C++ Connector 开发头   │
│ docker.io          │ Docker 引擎                 │
│ docker-compose-v2  │ Docker Compose 插件          │
│ curl               │ API 测试 (可选)              │
│ libgtest-dev       │ 单元测试 (可选)              │
└────────────────────┴────────────────────────────┘

┌─────────────────────────────────────────────────┐
│        CMake FetchContent (构建时自动拉取)         │
├─────────────────────────────────────────────────┤
│ cpp-httplib  v0.15.3   HTTP server (header-only)│
│ nlohmann/json v3.11.3  JSON 解析 (header-only)  │
│ yaml-cpp      0.8.0    YAML 解析 (种子数据)      │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│           Docker 镜像 (docker compose 自动拉取)    │
├─────────────────────────────────────────────────┤
│ nginx:alpine    反向代理 + 静态文件服务            │
│ mysql:8.0       数据库                           │
│ ubuntu:22.04    app 镜像构建基座                   │
└─────────────────────────────────────────────────┘
```

---

## 2. 逐步安装

### 2.1 系统包管理器更新

```bash
sudo apt update && sudo apt upgrade -y
```

### 2.2 构建工具链

```bash
sudo apt install -y build-essential cmake git pkg-config
```

| 包 | 版本 (Noble) | 用途 |
|----|-------------|------|
| build-essential | 12.10 | gcc-14, g++-14, make 4.3 |
| cmake | 3.28.3 | C++ 项目构建 |
| git | 2.43.0 | FetchContent 拉取依赖源码 |
| pkg-config | 1.8.1 | 辅助 CMake 查找库路径 |

### 2.3 系统库 (开发头文件)

```bash
sudo apt install -y libssl-dev libmysqlcppconn-dev
```

| 包 | 版本 (Noble) | 用途 |
|----|-------------|------|
| libssl-dev | 3.0.13 | OpenSSL (HMAC-SHA256 签名 JWT, bcrypt 哈希) |
| libmysqlcppconn-dev | 1.1.12 | MySQL C++ Connector (数据库连接) |

### 2.4 Docker 环境

```bash
sudo apt install -y docker.io docker-compose-v2
```

安装后将当前用户加入 docker 组以避免每次使用 `sudo`：

```bash
sudo usermod -aG docker $USER
```

**需要重新登录**使组成员身份生效，或执行 `newgrp docker` 在当前 shell 刷新。

| 包 | 版本 (Noble) | 用途 |
|----|-------------|------|
| docker.io | 29.1.3 | Docker 引擎 |
| docker-compose-v2 | 2.40.3 | Compose 插件 (docker compose 命令) |

### 2.5 可选工具

```bash
sudo apt install -y curl libgtest-dev
```

| 包 | 用途 |
|----|------|
| curl | API 端点手动测试 |
| libgtest-dev | Google Test 单元测试框架 |

---

## 3. 验证清单

逐条执行以下命令，确认每个依赖就绪。

### 3.1 构建工具链

```bash
g++ --version          # 预期: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 或更高
cmake --version        # 预期: cmake version 3.28.3 或更高
git --version          # 预期: git version 2.43.0 或更高
```

### 3.2 系统库

```bash
# OpenSSL 开发文件
ls /usr/include/openssl/ssl.h     # 应存在
dpkg -l libssl-dev | grep '^ii'   # 应显示已安装

# MySQL C++ Connector
ls /usr/include/mysql-cppconn-8/   # 应存在 (头文件目录)
dpkg -l libmysqlcppconn-dev | grep '^ii'  # 应显示已安装
```

### 3.3 Docker

```bash
docker --version       # 预期: Docker version 29.x.x
docker compose version # 预期: Docker Compose version v2.40.x
docker run hello-world # 预期: Hello from Docker! (验证引擎可用)
```

### 3.4 项目构建验证

```bash
cd /home/ljt/ljt/VibeOJ
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

构建成功后应生成 `build/vibeoj-server` 可执行文件：

```bash
ls -lh ./vibeoj-server   # 应存在且可执行
```

### 3.5 Docker Compose 全链路验证

```bash
cd /home/ljt/ljt/VibeOJ
docker compose up -d    # 启动全部服务
```

验证服务状态：

```bash
docker compose ps       # nginx, app, mysql 三个服务均为 Up 状态
curl http://localhost/api/v1/problems  # 应返回题目列表 JSON (可能为空数组 [])
```

停止服务：

```bash
docker compose down
```

---

## 4. 当前环境验证记录

> 以下为 2026-06-01 在开发机上实际验证的结果。

| 依赖 | 状态 | 备注 |
|------|:----:|------|
| build-essential | ✅ | g++ 13.3.0 |
| cmake | ✅ | 3.28.3 |
| git | ✅ | 2.43.0 |
| pkg-config | ✅ | 1.8.1 |
| libssl-dev | ✅ | 3.0.13 |
| libmysqlcppconn-dev | ✅ | 1.1.12 |
| docker.io | ✅ | 29.1.3 |
| docker-compose-v2 | ✅ | 2.40.3 |
| curl | ✅ | 8.5.0 |
| libgtest-dev | ✅ | 1.14.0 |

**所有依赖已就绪** ✅

---

## 5. 常见问题

### Q: CMake 找不到 mysql-connector-cpp？

Ubuntu 24.04 的 `libmysqlcppconn-dev` 将头文件安装在 `/usr/include/cppconn/`，MySQL 驱动头文件（`mysql_driver.h`、`mysql_connection.h`）位于 `/usr/include/`。CMakeLists.txt 通过 `find_library` 查找库文件，无需额外配置 include 路径。若查找失败，手动指定：

```bash
cmake .. -DMYSQL_CPPCONN_LIB=/usr/lib/x86_64-linux-gnu/libmysqlcppconn.so
```

### Q: Docker 权限被拒绝？

确认已执行 `sudo usermod -aG docker $USER` 并**重新登录**（或 `newgrp docker`）。

### Q: CMake FetchContent 下载失败？

cpp-httplib / nlohmann/json / yaml-cpp 均从 GitHub 拉取。如网络受限，可预先 clone 到本地并用 `FetchContent_Declare` 的 `SOURCE_DIR` 指向本地路径。
