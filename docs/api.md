# VibeOJ API 接口文档

Base URL: `/api/v1`

所有 API 响应体均为 JSON。除认证端点与健康检查外，均遵循统一响应信封格式。

---

## 目录

- [1. 响应信封规范](#1-响应信封规范)
- [2. HTTP 状态码使用规范](#2-http-状态码使用规范)
- [3. 认证体系](#3-认证体系)
- [4. 分页参数约定](#4-分页参数约定)
- [5. JSON 命名约定](#5-json-命名约定)
- [6. 健康检查](#6-健康检查)
- [7. 认证 API](#7-认证-api)
  - [POST /auth/register — 注册](#post-authregister--注册)
  - [POST /auth/login — 登录](#post-authlogin--登录)
  - [POST /auth/refresh — 刷新令牌](#post-authrefresh--刷新令牌)
  - [POST /auth/logout — 登出](#post-authlogout--登出)
- [8. 题目 API](#8-题目-api)
  - [GET /problems — 题目列表](#get-problems--题目列表)
  - [GET /problems/:id — 题目详情](#get-problemsid--题目详情)
- [9. 提交与判题 API](#9-提交与判题-api)
  - [POST /submissions — 提交代码](#post-submissions--提交代码)
  - [GET /submissions/:id — 提交详情](#get-submissionsid--提交详情)
  - [GET /submissions — 提交列表](#get-submissions--提交列表)
- [10. 管理后台 API](#10-管理后台-api)
  - [POST /admin/problems — 创建题目](#post-adminproblems--创建题目)
  - [PUT /admin/problems/:id — 编辑题目](#put-adminproblemsid--编辑题目)
  - [DELETE /admin/problems/:id — 删除题目](#delete-adminproblemsid--删除题目)
  - [POST /admin/problems/:id/testcases — 添加测试用例](#post-adminproblemsidtestcases--添加测试用例)
  - [PUT /admin/testcases/:id — 编辑测试用例](#put-admintestcasesid--编辑测试用例)
  - [DELETE /admin/testcases/:id — 删除测试用例](#delete-admintestcasesid--删除测试用例)
  - [GET /admin/users — 用户列表](#get-adminusers--用户列表)
  - [PUT /admin/users/:id — 修改用户](#put-adminusersid--修改用户)
  - [GET /admin/stats — 统计数据](#get-adminstats--统计数据)
- [11. 枚举值参考](#11-枚举值参考)
- [12. 检查清单（新增端点时自检）](#12-检查清单新增端点时自检)

---

## 1. 响应信封规范

> **注意：** 认证端点（`/auth/*`）与健康检查（`/health`）不使用此信封格式，详见各端点说明。

### 成功响应 (200/201)

```json
{
  "data": <any>,
  "message": "ok"
}
```

- `data` 字段承载实际的业务数据，可以是对象、数组或 null。
- `message` 固定为 `"ok"`。

### 列表（分页）响应 (200)

```json
{
  "data": {
    "items": [...],
    "total": 123,
    "page": 1,
    "page_size": 20
  },
  "message": "ok"
}
```

- `items` — 当前页记录数组。
- `total` — 符合条件的总记录数。
- `page` / `page_size` — 回显请求参数，便于前端计算总页数（`total_pages = ceil(total / page_size)`）。

### 错误响应 (4xx/5xx)

```json
{
  "error": "human-readable error message"
}
```

- HTTP 状态码同时反映错误类别。
- `error` 字段为面向开发者的英文错误描述。

---

## 2. HTTP 状态码使用规范

| 状态码 | 含义 | 使用场景 |
|--------|------|----------|
| 200 | OK | 请求成功（GET/PUT/DELETE） |
| 201 | Created | 资源创建成功（POST） |
| 400 | Bad Request | 请求参数非法或缺失 |
| 401 | Unauthorized | 缺少或无效的 access_token |
| 403 | Forbidden | 已认证但权限不足（非 admin 操作 admin 接口；账号被禁用） |
| 404 | Not Found | 资源不存在 |
| 409 | Conflict | 资源冲突（用户名已存在） |
| 500 | Internal Server Error | 服务器内部错误 |
| 503 | Service Unavailable | 数据库等依赖不可用 |

---

## 3. 认证体系

### Token 机制

| Token 类型 | 传输方式 | 默认有效期 | 用途 |
|------------|----------|------------|------|
| Access Token | `Authorization: Bearer <token>` header | 15 分钟 (900s) | 认证 API 请求 |
| Refresh Token | `httpOnly` Cookie (`refresh_token`) | 7 天 (604800s) | 换取新 Access Token |

Refresh Token Cookie 属性：
- `HttpOnly` — JS 不可访问
- `Path=/api/v1/auth` — 仅认证端点自动携带
- `SameSite=Strict` — CSRF 防护
- `Max-Age` — 与 JWT 有效期一致

### 认证流程

```
登录成功 → 响应体: { access_token (15min), user }
         → Set-Cookie: refresh_token (httpOnly, 7d)

API 请求 → Authorization: Bearer <access_token>
         → 401 → 前端自动 POST /auth/refresh (cookie 自动携带)
              → 成功 → 更新 access_token，重放原请求
              → 失败 → 跳转登录页
```

### 鉴权要求

| 端点类别 | 要求 |
|----------|------|
| 题目列表/详情 | 无需认证（可选） |
| 提交（创建/查询/列表） | Bearer Token（登录用户） |
| 管理后台（所有） | Bearer Token + admin 角色 |

---

## 4. 分页参数约定

| 查询参数 | 类型 | 默认值 | 说明 |
|----------|------|--------|------|
| `page` | int | 1 | 页码，从 1 开始，小于 1 自动修正为 1 |
| `page_size` | int | 20 | 每页条数，上限 100，超出自动截断为 100 |

- 若 `page` 超出有效范围，返回空列表（`items: [], total: N`）而非报错。

---

## 5. JSON 命名约定

- 所有字段使用 `snake_case`。
- 日期时间格式：`YYYY-MM-DD HH:MM:SS`（UTC），字符串类型。
- 布尔值使用 JSON 原生 `true`/`false`（`is_sample` 字段）。
- 枚举值使用小写字符串（如 `"easy"`、`"accepted"`）。

---

## 6. 健康检查

### GET /health

无需认证。用于负载均衡器或监控系统探测服务存活状态。

**请求**

```
GET /api/v1/health
```

**响应** (200)

```json
{
  "status": "ok"
}
```

> 此端点不使用标准 `{data, message}` 信封。

---

## 7. 认证 API

认证端点不使用标准 `{data, message}` 响应信封，而是采用各自的扁平 JSON 结构。

---

### POST /auth/register — 注册

创建新用户账号，默认角色为 `user`，状态为 `active`。

**请求**

```
POST /api/v1/auth/register
Content-Type: application/json

{
  "username": "alice",
  "password": "secret123"
}
```

| 字段 | 类型 | 必填 | 约束 |
|------|------|------|------|
| `username` | string | 是 | 1-64 字符 |
| `password` | string | 是 | 6-128 字符 |

**成功响应** (201)

```json
{
  "id": 1,
  "username": "alice",
  "role": "user"
}
```

**错误响应**

| 状态码 | error | 原因 |
|--------|-------|------|
| 400 | `invalid JSON body` | 请求体不是合法 JSON |
| 400 | `username must be 1-64 characters` | 用户名为空或超过 64 字符 |
| 400 | `password must be 6-128 characters` | 密码长度不符合要求 |
| 409 | `username already exists` | 用户名已被占用 |
| 500 | `internal error` | 密码哈希失败 |
| 500 | `failed to create user` | 数据库插入失败 |
| 503 | `database unavailable` | 无法获取数据库连接 |

---

### POST /auth/login — 登录

验证用户凭据，返回 Access Token 并设置 Refresh Token Cookie。

**请求**

```
POST /api/v1/auth/login
Content-Type: application/json

{
  "username": "alice",
  "password": "secret123"
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `username` | string | 是 | |
| `password` | string | 是 | |

**成功响应** (200)

```json
{
  "access_token": "eyJhbGciOiJIUzI1NiIs...",
  "token_type": "Bearer",
  "expires_in": 900,
  "user": {
    "id": 1,
    "username": "alice",
    "role": "user"
  }
}
```

同时设置 Cookie：
```
Set-Cookie: refresh_token=<token>; HttpOnly; Path=/api/v1/auth; Max-Age=604800; SameSite=Strict
```

| 响应字段 | 类型 | 说明 |
|----------|------|------|
| `access_token` | string | JWT，后续请求放入 `Authorization: Bearer <token>` |
| `token_type` | string | 固定为 `"Bearer"` |
| `expires_in` | int | Access Token 有效期（秒），默认 900 |
| `user.id` | int | 用户 ID |
| `user.username` | string | 用户名 |
| `user.role` | string | `"user"` 或 `"admin"` |

**错误响应**

| 状态码 | error | 原因 |
|--------|-------|------|
| 400 | `invalid JSON body` | 请求体不是合法 JSON |
| 400 | `username and password required` | 缺少用户名或密码 |
| 401 | `invalid username or password` | 用户名不存在或密码错误 |
| 403 | `account is disabled` | 账号已被管理员禁用 |
| 503 | `database unavailable` | 无法获取数据库连接 |

---

### POST /auth/refresh — 刷新令牌

使用 Cookie 中的 Refresh Token 换取新的 Access Token。Refresh Token 不会被轮换（保持原有有效期）。

**请求**

```
POST /api/v1/auth/refresh
Cookie: refresh_token=<token>
```

请求体为空。Cookie 由浏览器自动携带（Path 限定为 `/api/v1/auth`）。

**成功响应** (200)

```json
{
  "access_token": "eyJhbGciOiJIUzI1NiIs...",
  "token_type": "Bearer",
  "expires_in": 900
}
```

**错误响应**

| 状态码 | error | 原因 |
|--------|-------|------|
| 401 | `no refresh token` | Cookie 中未携带 refresh_token |
| 401 | `invalid or expired refresh token` | Refresh Token JWT 无效或已过期 |
| 401 | `refresh token revoked` | Refresh Token 已被登出撤销（数据库中不存在） |
| 401 | `user not found` | Token 中的用户 ID 在数据库中不存在 |
| 403 | `account is disabled` | 账号已被管理员禁用 |
| 503 | `database unavailable` | 无法获取数据库连接 |

---

### POST /auth/logout — 登出

撤销当前 Refresh Token 并清除 Cookie。后续 refresh 请求将失败。

**请求**

```
POST /api/v1/auth/logout
Cookie: refresh_token=<token>
```

请求体为空。

**成功响应** (200)

```json
{
  "message": "logged out"
}
```

同时清除 Cookie：
```
Set-Cookie: refresh_token=; HttpOnly; Path=/api/v1/auth; Max-Age=0; SameSite=Strict
```

- 即使 Cookie 中没有 refresh_token，该接口也返回 200（幂等操作）。

---

## 8. 题目 API

题目 API 无需认证即可访问。

---

### GET /problems — 题目列表

获取题目分页列表，支持按难度筛选。列表项仅包含摘要信息，不含题目描述和测试用例。

**请求**

```
GET /api/v1/problems?difficulty=easy&page=1&page_size=20
```

| 查询参数 | 类型 | 默认值 | 说明 |
|----------|------|--------|------|
| `difficulty` | string | 无（全部） | 可选筛选：`easy` / `medium` / `hard` |
| `page` | int | 1 | 页码 |
| `page_size` | int | 20 | 每页条数，上限 100 |

**成功响应** (200)

```json
{
  "data": {
    "items": [
      {
        "id": 1,
        "title": "两数之和",
        "difficulty": "easy",
        "created_at": "2026-06-01 10:00:00"
      }
    ],
    "total": 1,
    "page": 1,
    "page_size": 20
  },
  "message": "ok"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `items[].id` | int | 题目 ID |
| `items[].title` | string | 题目标题 |
| `items[].difficulty` | string | `easy` / `medium` / `hard` |
| `items[].created_at` | string | 创建时间 `YYYY-MM-DD HH:MM:SS` |
| `total` | int | 符合筛选条件的总题目数 |
| `page` | int | 当前页码（回显） |
| `page_size` | int | 每页条数（回显） |

**错误响应**

| 状态码 | error | 原因 |
|--------|-------|------|
| 400 | `invalid difficulty: must be easy/medium/hard` | difficulty 参数值不合法 |
| 503 | `database unavailable` | 无法获取数据库连接 |

---

### GET /problems/:id — 题目详情

获取题目完整信息，包括描述、限制条件和可见的样例测试用例（`is_sample=true`）。

**请求**

```
GET /api/v1/problems/1
```

**成功响应** (200)

```json
{
  "data": {
    "id": 1,
    "title": "两数之和",
    "description": "给定一个整数数组 nums 和一个整数目标值 target...（Markdown 格式）",
    "difficulty": "easy",
    "time_limit_ms": 1000,
    "memory_limit_kb": 262144,
    "created_at": "2026-06-01 10:00:00",
    "sample_cases": [
      {
        "id": 1,
        "input": "3\n2 7 11 15\n9\n",
        "expected_output": "0 1\n",
        "order_index": 1
      }
    ]
  },
  "message": "ok"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | int | 题目 ID |
| `title` | string | 题目标题 |
| `description` | string | 题目描述（Markdown 原始文本） |
| `difficulty` | string | `easy` / `medium` / `hard` |
| `time_limit_ms` | int | 时间限制（毫秒） |
| `memory_limit_kb` | int | 内存限制（KB） |
| `created_at` | string | 创建时间 `YYYY-MM-DD HH:MM:SS` |
| `sample_cases` | array | **仅包含 is_sample=true 的测试用例** |
| `sample_cases[].id` | int | 测试用例 ID |
| `sample_cases[].input` | string | 标准输入内容 |
| `sample_cases[].expected_output` | string | 期望的标准输出 |
| `sample_cases[].order_index` | int | 用例排序序号 |

**错误响应**

| 状态码 | error | 原因 |
|--------|-------|------|
| 404 | `problem not found` | 题目不存在 |
| 503 | `database unavailable` | 无法获取数据库连接 |

---

## 9. 提交与判题 API

提交相关接口需要 Bearer Token 认证。

---

### POST /submissions — 提交代码

提交 C++ 代码进行判题。提交创建后立即进入判题队列，后端异步执行编译和测试。

**请求**

```
POST /api/v1/submissions
Authorization: Bearer <access_token>
Content-Type: application/json

{
  "problem_id": 1,
  "code": "#include <iostream>\nusing namespace std;\nint main() {\n  int n;\n  cin >> n;\n  cout << n * 2 << endl;\n  return 0;\n}"
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `problem_id` | int | 是 | 题目 ID |
| `code` | string | 是 | C++ 源代码，不能为空 |

**成功响应** (201)

```json
{
  "data": {
    "submission_id": 42
  },
  "message": "ok"
}
```

提交创建后状态为 `pending`，判题引擎将自动处理。前端可通过 `GET /submissions/:id` 轮询获取判题结果。

**错误响应**

| 状态码 | error | 原因 |
|--------|-------|------|
| 400 | `invalid JSON body` | 请求体不是合法 JSON |
| 400 | `problem_id is required and must be a number` | 缺少 problem_id 或类型错误 |
| 400 | `code is required and must not be empty` | 缺少 code 或为空字符串 |
| 401 | `authentication required` | 未提供有效 Bearer Token |
| 404 | `problem not found` | 题目不存在 |
| 500 | `failed to create submission` | 数据库插入失败 |
| 503 | `database unavailable` | 无法获取数据库连接 |

---

### GET /submissions/:id — 提交详情

查询单条提交的完整信息，包括判题状态、编译输出、耗时和内存等。

**请求**

```
GET /api/v1/submissions/42
Authorization: Bearer <access_token>
```

**成功响应** (200)

```json
{
  "data": {
    "id": 42,
    "user_id": 1,
    "problem_id": 1,
    "code": "#include <iostream>...",
    "status": "accepted",
    "compile_output": "",
    "passed_cases": 5,
    "total_cases": 5,
    "time_used_ms": 12,
    "memory_used_kb": 4096,
    "diff_output": "",
    "created_at": "2026-06-01 12:00:00",
    "updated_at": "2026-06-01 12:00:03"
  },
  "message": "ok"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | int | 提交 ID |
| `user_id` | int | 提交者用户 ID |
| `problem_id` | int | 题目 ID |
| `code` | string | 提交的源代码 |
| `status` | string | 判题状态，参见 [枚举值参考](#11-枚举值参考) |
| `compile_output` | string | g++ 编译错误输出（`compile_error` 时有内容，否则为空） |
| `passed_cases` | int | 通过的测试用例数 |
| `total_cases` | int | 总测试用例数 |
| `time_used_ms` | int | 所有测试用例中的最大耗时（毫秒） |
| `memory_used_kb` | int | 所有测试用例中的最大内存（KB） |
| `diff_output` | string | WA 时的 diff 信息（`wrong_answer` 时有内容） |
| `created_at` | string | 提交时间 `YYYY-MM-DD HH:MM:SS` |
| `updated_at` | string | 最后状态更新时间 `YYYY-MM-DD HH:MM:SS` |

**访问控制：** 普通用户只能查看自己的提交，admin 可查看任何人。

**错误响应**

| 状态码 | error | 原因 |
|--------|-------|------|
| 401 | `authentication required` | 未提供有效 Bearer Token |
| 403 | `access denied` | 非 admin 用户尝试查看他人提交 |
| 404 | `submission not found` | 提交不存在 |
| 503 | `database unavailable` | 无法获取数据库连接 |

---

### GET /submissions — 提交列表

获取提交分页列表，支持按题目和用户筛选。

**请求**

```
GET /api/v1/submissions?problem_id=1&user_id=1&page=1&page_size=20
Authorization: Bearer <access_token>
```

| 查询参数 | 类型 | 默认值 | 说明 |
|----------|------|--------|------|
| `problem_id` | int | 无 | 筛选指定题目的提交 |
| `user_id` | int | 无 | 筛选指定用户的提交 |
| `page` | int | 1 | 页码 |
| `page_size` | int | 20 | 每页条数，上限 100 |

**访问控制：**
- 普通用户：`user_id` 参数被忽略，始终只返回自己的提交。
- Admin：可指定 `user_id` 查看他人提交；不指定则返回所有人的提交。

**成功响应** (200)

```json
{
  "data": {
    "items": [
      {
        "id": 42,
        "user_id": 1,
        "problem_id": 1,
        "code": "#include <iostream>...",
        "status": "accepted",
        "compile_output": "",
        "passed_cases": 5,
        "total_cases": 5,
        "time_used_ms": 12,
        "memory_used_kb": 4096,
        "diff_output": "",
        "created_at": "2026-06-01 12:00:00",
        "updated_at": "2026-06-01 12:00:03"
      }
    ],
    "total": 1,
    "page": 1,
    "page_size": 20
  },
  "message": "ok"
}
```

列表中每个 item 的字段与 [提交详情](#get-submissionsid--提交详情) 完全一致。

**错误响应**

| 状态码 | error | 原因 |
|--------|-------|------|
| 400 | `invalid user_id` | user_id 参数格式错误 |
| 400 | `invalid problem_id` | problem_id 参数格式错误 |
| 401 | `authentication required` | 未提供有效 Bearer Token |
| 503 | `database unavailable` | 无法获取数据库连接 |

---

## 10. 管理后台 API

所有管理后台接口需要 Bearer Token + admin 角色。缺少任一条件时：
- 无 Token → 401 `{"error": "authentication required"}`
- 非 admin → 403 `{"error": "admin privileges required"}`

以下各端点不再重复这两条错误响应。

---

### POST /admin/problems — 创建题目

创建新题目，可同时附带测试用例数组。

**请求**

```
POST /api/v1/admin/problems
Authorization: Bearer <access_token>
Content-Type: application/json

{
  "title": "两数之和",
  "description": "给定一个整数数组 nums...（Markdown 格式）",
  "difficulty": "easy",
  "time_limit_ms": 1000,
  "memory_limit_kb": 262144,
  "test_cases": [
    {
      "input": "3\n2 7 11 15\n9\n",
      "expected_output": "0 1\n",
      "is_sample": true,
      "order_index": 1
    }
  ]
}
```

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| `title` | string | **是** | — | 题目标题 |
| `description` | string | **是** | — | 题目描述（Markdown） |
| `difficulty` | string | 否 | `"easy"` | `easy` / `medium` / `hard` |
| `time_limit_ms` | int | 否 | 1000 | 时间限制（毫秒） |
| `memory_limit_kb` | int | 否 | 262144 | 内存限制（KB，默认 256MB） |
| `test_cases` | array | 否 | `[]` | 测试用例数组（可追加或后续单独添加） |
| `test_cases[].input` | string | 否 | `""` | 标准输入 |
| `test_cases[].expected_output` | string | 否 | `""` | 期望输出 |
| `test_cases[].is_sample` | bool | 否 | `false` | 是否对用户可见 |
| `test_cases[].order_index` | int | 否 | 0 | 排序序号 |

**成功响应** (201)

```json
{
  "data": {
    "id": 1,
    "title": "两数之和",
    "description": "给定一个整数数组 nums...",
    "difficulty": "easy",
    "time_limit_ms": 1000,
    "memory_limit_kb": 262144,
    "created_by": 1,
    "created_at": "2026-06-01 12:00:00",
    "test_cases": [
      {
        "id": 1,
        "problem_id": 1,
        "input": "3\n2 7 11 15\n9\n",
        "expected_output": "0 1\n",
        "is_sample": true,
        "order_index": 1
      }
    ]
  },
  "message": "ok"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | int | 新创建的题目 ID |
| `created_by` | int | 创建者（当前 admin 用户 ID） |
| `created_at` | string | 创建时间 |
| `test_cases` | array | 成功创建的测试用例列表（含分配的 ID） |

**错误响应**

| 状态码 | error | 原因 |
|--------|-------|------|
| 400 | `invalid JSON body` | 请求体不是合法 JSON |
| 400 | `title is required` | 缺少标题或类型错误 |
| 400 | `description is required` | 缺少描述或类型错误 |
| 400 | `invalid difficulty: must be easy/medium/hard` | difficulty 值不合法 |
| 400 | `time_limit_ms must be a number` | 类型错误 |
| 400 | `memory_limit_kb must be a number` | 类型错误 |
| 500 | `failed to create problem` | 数据库插入失败 |
| 503 | `database unavailable` | 无法获取数据库连接 |

---

### PUT /admin/problems/:id — 编辑题目

更新已有题目的全部字段。所有业务字段均需提供（整体替换语义）。

**请求**

```
PUT /api/v1/admin/problems/1
Authorization: Bearer <access_token>
Content-Type: application/json

{
  "title": "两数之和（修订版）",
  "description": "更新后的描述...",
  "difficulty": "medium",
  "time_limit_ms": 2000,
  "memory_limit_kb": 524288
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `title` | string | **是** | 新标题 |
| `description` | string | **是** | 新描述 |
| `difficulty` | string | 否（默认 easy） | 新难度 |
| `time_limit_ms` | int | 否（默认 1000） | 新时间限制 |
| `memory_limit_kb` | int | 否（默认 262144） | 新内存限制 |

**成功响应** (200)

```json
{
  "data": {
    "id": 1,
    "title": "两数之和（修订版）",
    "description": "更新后的描述...",
    "difficulty": "medium",
    "time_limit_ms": 2000,
    "memory_limit_kb": 524288,
    "created_by": 1,
    "created_at": "2026-06-01 12:00:00"
  },
  "message": "ok"
}
```

**错误响应**

| 状态码 | error | 原因 |
|--------|-------|------|
| 400 | 同创建题目 | 参数校验失败 |
| 404 | `problem not found` | 题目不存在 |
| 500 | `failed to update problem` | 数据库更新失败 |
| 503 | `database unavailable` | 无法获取数据库连接 |

---

### DELETE /admin/problems/:id — 删除题目

删除题目及其关联的所有测试用例（级联删除）。

**请求**

```
DELETE /api/v1/admin/problems/1
Authorization: Bearer <access_token>
```

**成功响应** (200)

```json
{
  "data": {
    "deleted": 1
  },
  "message": "ok"
}
```

- `deleted` — 被删除的题目 ID。

**错误响应**

| 状态码 | error | 原因 |
|--------|-------|------|
| 404 | `problem not found` | 题目不存在 |
| 500 | `failed to delete problem` | 数据库删除失败 |
| 503 | `database unavailable` | 无法获取数据库连接 |

---

### POST /admin/problems/:id/testcases — 添加测试用例

为指定题目添加一个测试用例。

**请求**

```
POST /api/v1/admin/problems/1/testcases
Authorization: Bearer <access_token>
Content-Type: application/json

{
  "input": "3\n1 2 3\n6\n",
  "expected_output": "0 2\n",
  "is_sample": false,
  "order_index": 2
}
```

| 字段 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| `input` | string | **是** | — | 标准输入 |
| `expected_output` | string | **是** | — | 期望输出 |
| `is_sample` | bool | 否 | `false` | 是否对用户可见 |
| `order_index` | int | 否 | 0 | 排序序号 |

**成功响应** (201)

```json
{
  "data": {
    "id": 3,
    "problem_id": 1,
    "input": "3\n1 2 3\n6\n",
    "expected_output": "0 2\n",
    "is_sample": false,
    "order_index": 2
  },
  "message": "ok"
}
```

**错误响应**

| 状态码 | error | 原因 |
|--------|-------|------|
| 400 | `invalid JSON body` | 请求体不是合法 JSON |
| 400 | `input is required` | 缺少 input 字段 |
| 400 | `expected_output is required` | 缺少 expected_output 字段 |
| 404 | `problem not found` | 题目不存在 |
| 500 | `failed to create test case` | 数据库插入失败 |
| 503 | `database unavailable` | 无法获取数据库连接 |

---

### PUT /admin/testcases/:id — 编辑测试用例

更新已有测试用例。请求体中 `problem_id` 为必填项（用于定位用例所在题目），其余字段可选（未提供则保持原值）。

**请求**

```
PUT /api/v1/admin/testcases/3
Authorization: Bearer <access_token>
Content-Type: application/json

{
  "problem_id": 1,
  "input": "3\n4 5 6\n15\n",
  "expected_output": "0 2\n",
  "is_sample": true,
  "order_index": 1
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `problem_id` | int | **是** | 所属题目 ID（用于定位验证） |
| `input` | string | 否 | 新标准输入（不提供则保留原值） |
| `expected_output` | string | 否 | 新期望输出（不提供则保留原值） |
| `is_sample` | bool | 否 | 是否可见（不提供则保留原值） |
| `order_index` | int | 否 | 排序序号（不提供则保留原值） |

**成功响应** (200)

```json
{
  "data": {
    "id": 3,
    "problem_id": 1,
    "input": "3\n4 5 6\n15\n",
    "expected_output": "0 2\n",
    "is_sample": true,
    "order_index": 1
  },
  "message": "ok"
}
```

**错误响应**

| 状态码 | error | 原因 |
|--------|-------|------|
| 400 | `invalid JSON body` | 请求体不是合法 JSON |
| 400 | `problem_id is required` | 缺少 problem_id 字段 |
| 404 | `test case not found` | 测试用例不存在（指定 problem_id 下未找到该 id） |
| 500 | `failed to update test case` | 数据库更新失败 |
| 503 | `database unavailable` | 无法获取数据库连接 |

---

### DELETE /admin/testcases/:id — 删除测试用例

删除指定测试用例。

**请求**

```
DELETE /api/v1/admin/testcases/3
Authorization: Bearer <access_token>
```

**成功响应** (200)

```json
{
  "data": {
    "deleted": 3
  },
  "message": "ok"
}
```

- `deleted` — 被删除的测试用例 ID。

**错误响应**

| 状态码 | error | 原因 |
|--------|-------|------|
| 404 | `test case not found` | 测试用例不存在 |
| 503 | `database unavailable` | 无法获取数据库连接 |

---

### GET /admin/users — 用户列表

获取所有用户的列表（不含密码哈希）。**不支持分页。**

**请求**

```
GET /api/v1/admin/users
Authorization: Bearer <access_token>
```

**成功响应** (200)

```json
{
  "data": {
    "items": [
      {
        "id": 1,
        "username": "alice",
        "role": "user",
        "status": "active",
        "created_at": "2026-06-01 10:00:00"
      }
    ],
    "total": 1
  },
  "message": "ok"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `items[].id` | int | 用户 ID |
| `items[].username` | string | 用户名 |
| `items[].role` | string | `user` / `admin` |
| `items[].status` | string | `active` / `disabled` |
| `items[].created_at` | string | 注册时间 |
| `total` | int | 用户总数 |

> 注意：此接口无 `page` / `page_size` 字段。

**错误响应**

| 状态码 | error | 原因 |
|--------|-------|------|
| 503 | `database unavailable` | 无法获取数据库连接 |

---

### PUT /admin/users/:id — 修改用户

修改用户的角色和/或状态。至少需要提供 `role` 或 `status` 之一。

**请求**

```
PUT /api/v1/admin/users/1
Authorization: Bearer <access_token>
Content-Type: application/json

{
  "role": "admin",
  "status": "active"
}
```

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `role` | string | 否 | `admin` / `user` |
| `status` | string | 否 | `active` / `disabled` |

至少需要提供一个字段；两个都提供亦可。

**成功响应** (200)

```json
{
  "data": {
    "id": 1,
    "username": "alice",
    "role": "admin",
    "status": "active",
    "created_at": "2026-06-01 10:00:00"
  },
  "message": "ok"
}
```

**错误响应**

| 状态码 | error | 原因 |
|--------|-------|------|
| 400 | `invalid JSON body` | 请求体不是合法 JSON |
| 400 | `invalid role: must be admin or user` | role 参数值不合法 |
| 400 | `invalid status: must be active or disabled` | status 参数值不合法 |
| 400 | `role or status is required` | 两个字段均未提供 |
| 404 | `user not found` | 用户不存在 |
| 503 | `database unavailable` | 无法获取数据库连接 |

---

### GET /admin/stats — 统计数据

获取平台全局统计数据。

**请求**

```
GET /api/v1/admin/stats
Authorization: Bearer <access_token>
```

**成功响应** (200)

```json
{
  "data": {
    "total_users": 10,
    "total_problems": 5,
    "total_submissions": 123,
    "submissions_by_status": {
      "pending": 0,
      "compiling": 0,
      "running": 1,
      "accepted": 80,
      "wrong_answer": 30,
      "time_limit": 5,
      "memory_limit": 2,
      "runtime_error": 3,
      "compile_error": 2,
      "system_error": 0
    },
    "daily_trend": [
      { "date": "2026-05-26", "count": 12 },
      { "date": "2026-05-27", "count": 18 },
      { "date": "2026-05-28", "count": 15 },
      { "date": "2026-05-29", "count": 20 },
      { "date": "2026-05-30", "count": 22 },
      { "date": "2026-05-31", "count": 16 },
      { "date": "2026-06-01", "count": 20 }
    ]
  },
  "message": "ok"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `total_users` | int | 注册用户总数 |
| `total_problems` | int | 题目总数 |
| `total_submissions` | int | 提交总数（所有状态） |
| `submissions_by_status` | object | 按判题状态分组的提交数量；key 为状态字符串，value 为数量 |
| `daily_trend` | array | 最近 7 天每日提交趋势，按日期升序 |
| `daily_trend[].date` | string | 日期 `YYYY-MM-DD` |
| `daily_trend[].count` | int | 当天提交数量 |

**错误响应**

| 状态码 | error | 原因 |
|--------|-------|------|
| 500 | `failed to query statistics` | SQL 查询异常 |
| 503 | `database unavailable` | 无法获取数据库连接 |

---

## 11. 枚举值参考

### 题目难度 (Difficulty)

| 值 | 含义 |
|----|------|
| `easy` | 简单 |
| `medium` | 中等 |
| `hard` | 困难 |

### 用户角色 (Role)

| 值 | 含义 |
|----|------|
| `user` | 普通用户 |
| `admin` | 管理员 |

### 用户状态 (UserStatus)

| 值 | 含义 |
|----|------|
| `active` | 正常 |
| `disabled` | 已禁用 |

### 提交/判题状态 (SubmissionStatus)

| 值 | 含义 | 说明 |
|----|------|------|
| `pending` | 等待中 | 提交已创建，等待判题线程处理 |
| `compiling` | 编译中 | g++ 正在编译 |
| `running` | 运行中 | 正在执行测试用例 |
| `accepted` | 通过 | 全部测试用例通过 |
| `wrong_answer` | 答案错误 | 输出与期望不符，详见 `diff_output` |
| `time_limit` | 超时 | 程序运行超过时间限制 |
| `memory_limit` | 超内存 | 程序内存使用超过限制 |
| `runtime_error` | 运行错误 | 程序异常退出（非 0 返回码或信号终止） |
| `compile_error` | 编译错误 | g++ 编译失败，详见 `compile_output` |
| `system_error` | 系统错误 | 判题引擎内部异常（非用户代码问题） |

---

## 12. 检查清单（新增端点时自检）

- [ ] 成功响应是否包含 `data` + `message: "ok"`？
- [ ] 列表接口是否使用 `{items, total, page, page_size}` 结构？
- [ ] 错误响应是否包含 `error` 字段 + 正确的 HTTP 状态码？
- [ ] 字段名是否使用 `snake_case`？
- [ ] 日期时间格式是否为 `YYYY-MM-DD HH:MM:SS`？
- [ ] 布尔值是否使用 JSON 原生 `true`/`false`？
- [ ] 分页参数是否支持 `page` / `page_size`，默认值是否为 1/20，上限 100？
- [ ] 需要认证的端点是否加了 Bearer Token 校验？
- [ ] Admin 端点是否加了角色权限校验？
- [ ] 错误消息是否正确映射到 [状态码表](#2-http-状态码使用规范)？
