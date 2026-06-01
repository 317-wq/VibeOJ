"""
API 客户端 — 封装 requests.Session，提供便捷的 HTTP 方法与认证管理。

用法:
    client = ApiClient("http://localhost:8080/api/v1")
    client.set_auth(access_token)

    resp = client.get("/problems")
    resp = client.post("/auth/login", json={"username": "...", "password": "..."})
"""

import time
from typing import Optional

import requests


class ApiClient:
    """基于 requests.Session 的 API 客户端。

    - 自动管理 Cookie（refresh_token cookie 由 Session 自动维护）
    - 自动添加 Content-Type: application/json
    - 支持 Bearer Token 认证
    - 默认超时保护
    - 遇到 503 自动重试（最多 3 次）
    """

    def __init__(self, base_url: str, timeout: int = 10):
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout
        self.session = requests.Session()
        self.session.headers["Content-Type"] = "application/json"
        self._auth_token: Optional[str] = None

    # ── 认证方法 ──────────────────────────────────────────────────

    def set_auth(self, token: str):
        """设置 Bearer Token，后续请求自动携带 Authorization header。"""
        self._auth_token = token
        self.session.headers["Authorization"] = f"Bearer {token}"

    def clear_auth(self):
        """清除认证 token。"""
        self._auth_token = None
        self.session.headers.pop("Authorization", None)

    @property
    def is_authenticated(self) -> bool:
        return self._auth_token is not None

    # ── HTTP 方法 ─────────────────────────────────────────────────

    def _url(self, path: str) -> str:
        return f"{self.base_url}{path}"

    def _retry_on_503(self, method: str, url: str, **kwargs) -> requests.Response:
        """遇到 503 自动重试最多 3 次，指数退避。"""
        max_retries = 3
        for attempt in range(max_retries):
            resp = self.session.request(method, url, timeout=self.timeout, **kwargs)
            if resp.status_code != 503 or attempt == max_retries - 1:
                return resp
            time.sleep(2 ** attempt)  # 1s, 2s, 4s
        return resp  # unreachable but safety

    def get(self, path: str, **kwargs) -> requests.Response:
        return self._retry_on_503("GET", self._url(path), **kwargs)

    def post(self, path: str, **kwargs) -> requests.Response:
        return self._retry_on_503("POST", self._url(path), **kwargs)

    def put(self, path: str, **kwargs) -> requests.Response:
        return self._retry_on_503("PUT", self._url(path), **kwargs)

    def delete(self, path: str, **kwargs) -> requests.Response:
        return self._retry_on_503("DELETE", self._url(path), **kwargs)

    # ── 便捷方法 ──────────────────────────────────────────────────

    def login(self, username: str, password: str) -> requests.Response:
        """登录并返回响应。Cookie 由 Session 自动保存。"""
        return self.post("/auth/login", json={
            "username": username,
            "password": password,
        })

    def register(self, username: str, password: str) -> requests.Response:
        """注册新用户。"""
        return self.post("/auth/register", json={
            "username": username,
            "password": password,
        })

    def refresh(self) -> requests.Response:
        """刷新 access_token（需要 Cookie 中有 refresh_token）。"""
        return self.post("/auth/refresh")

    def logout(self) -> requests.Response:
        """登出，撤销 refresh_token。"""
        return self.post("/auth/logout")
