# VibeOJ API 统一接口约束

Base URL: `/api/v1`

所有 API 响应体均为 JSON，遵循以下统一格式。新增端点必须遵守此约定，确保前后端对接一致。

---

## 1. 响应信封

### 成功响应

```json
{
  "data": <any>,
  "message": "ok"
}
```

- `data` 字段承载实际的业务数据，可以是对象、数组或 null。
- `message` 固定为 `"ok"`，仅便于调试。

列表接口（分页）：

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

- `total` 为符合条件的总记录数。
- `page` / `page_size` 回显请求参数，便于前端计算总页数。

### 错误响应

```json
{
  "error": "human-readable error message"
}
```

- HTTP 状态码同时反映错误类别（4xx 客户端错误，5xx 服务端错误）。
- `error` 字段为面向开发者的错误描述，**不**直接展示给最终用户（多语言场景）。

---

## 2. HTTP 状态码使用规范

| 状态码 | 含义 | 使用场景 |
|--------|------|----------|
| 200 | OK | 请求成功（GET/PUT/DELETE） |
| 201 | Created | 资源创建成功（POST） |
| 204 | No Content | 操作成功但不返回 body（如删除） |
| 400 | Bad Request | 请求参数非法或缺失 |
| 401 | Unauthorized | 缺少或无效的 access_token |
| 403 | Forbidden | 已认证但权限不足（非 admin 操作 admin 接口） |
| 404 | Not Found | 资源不存在 |
| 409 | Conflict | 资源冲突（如用户名已存在） |
| 422 | Unprocessable Entity | 参数语义正确但无法处理 |
| 429 | Too Many Requests | 频率限制 |
| 500 | Internal Server Error | 服务器内部错误 |
| 503 | Service Unavailable | 数据库等依赖不可用 |

---

## 3. 认证

- Access Token 通过 `Authorization: Bearer <token>` 请求头传递。
- Refresh Token 通过 `httpOnly` Cookie 传递，Path 限定为 `/api/v1/auth`。
- Access Token 过期后前端应自动调用 `POST /auth/refresh` 换取新 token，然后重放原请求。

---

## 4. 分页参数约定

| 查询参数 | 类型 | 默认值 | 说明 |
|----------|------|--------|------|
| `page` | int | 1 | 页码，从 1 开始 |
| `page_size` | int | 20 | 每页条数，上限 100 |

- 若 `page` 超出有效范围，返回空列表而非报错。

---

## 5. JSON 命名约定

- 所有字段使用 `snake_case` 命名。
- 日期时间格式：`YYYY-MM-DD HH:MM:SS`（UTC），字符串类型。
- 布尔值使用 JSON 原生 `true`/`false`，不转换为 0/1。

---

## 6. 示例 — 题目列表

**请求**

```
GET /api/v1/problems?difficulty=easy&page=1&page_size=10
```

**响应** (200)

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
    "page_size": 10
  },
  "message": "ok"
}
```

**响应** — 请求不存在的题目 (404)

```json
{
  "error": "problem not found"
}
```

---

## 7. 检查清单（新增端点时自检）

- [ ] 成功响应是否包含 `data` + `message: "ok"`？
- [ ] 列表接口是否使用 `{items, total, page, page_size}` 结构？
- [ ] 错误响应是否包含 `error` 字段？
- [ ] 字段名是否使用 `snake_case`？
- [ ] HTTP 状态码是否符合上表？
- [ ] 分页参数是否支持 `page` / `page_size`？
