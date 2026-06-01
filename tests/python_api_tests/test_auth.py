"""
认证 API 测试 — POST /api/v1/auth/*
覆盖: register, login, refresh, logout 的成功与错误场景
"""

import time

import pytest


# ═══════════════════════════════════════════════════════════════════════
# Registration
# ═══════════════════════════════════════════════════════════════════════

class TestRegister:
    """POST /api/v1/auth/register"""

    @pytest.mark.smoke
    def test_register_success(self, api):
        """注册新用户返回 201，包含 id/username/role"""
        ts = int(time.time())
        resp = api.register(f"regtest_{ts}", "pass123456")

        assert resp.status_code == 201, f"Expected 201, got {resp.status_code}: {resp.text}"
        data = resp.json()
        assert "id" in data
        assert data["username"] == f"regtest_{ts}"
        assert data["role"] == "user"

    def test_register_empty_username(self, api):
        """空用户名返回 400"""
        resp = api.register("", "pass123456")
        assert resp.status_code == 400
        assert "username" in resp.json()["error"].lower()

    def test_register_short_password(self, api):
        """密码过短返回 400"""
        resp = api.register("testuser2", "ab")
        assert resp.status_code == 400
        assert "password" in resp.json()["error"].lower()

    def test_register_duplicate_username(self, api, test_user):
        """重复注册返回 409"""
        resp = api.register(test_user["username"], "somepass123")
        assert resp.status_code == 409
        assert "already exists" in resp.json()["error"].lower()

    def test_register_missing_fields(self, api):
        """缺少字段返回 400"""
        resp = api.post("/auth/register", json={})
        assert resp.status_code == 400


# ═══════════════════════════════════════════════════════════════════════
# Login
# ═══════════════════════════════════════════════════════════════════════

class TestLogin:
    """POST /api/v1/auth/login"""

    @pytest.mark.smoke
    def test_login_success(self, api, test_user):
        """登录成功返回 access_token + user 对象，同时设置 Cookie"""
        resp = api.login(test_user["username"], test_user["password"])

        assert resp.status_code == 200
        data = resp.json()
        assert "access_token" in data
        assert data["token_type"] == "Bearer"
        assert "expires_in" in data
        assert data["user"]["username"] == test_user["username"]
        assert data["user"]["role"] in ("user", "admin")

    def test_login_wrong_password(self, api, test_user):
        """错误密码返回 401"""
        resp = api.login(test_user["username"], "wrongpassword")
        assert resp.status_code == 401
        assert "invalid username or password" == resp.json()["error"]

    def test_login_nonexistent_user(self, api):
        """不存在的用户返回 401"""
        resp = api.login("nonexistent_user_999999", "pass123")
        assert resp.status_code == 401

    def test_login_missing_password(self, api):
        """缺少密码返回 400"""
        resp = api.post("/auth/login", json={"username": "test"})
        assert resp.status_code == 400
        assert "username and password required" == resp.json()["error"]

    def test_login_empty_body(self, api):
        """空请求体返回 400"""
        resp = api.post("/auth/login", json={})
        assert resp.status_code == 400

    def test_login_invalid_json(self, api):
        """非法 JSON 返回 400"""
        resp = api.session.post(
            f"{api.base_url}/auth/login",
            data="not json",
            headers={"Content-Type": "application/json"},
            timeout=api.timeout,
        )
        assert resp.status_code == 400


# ═══════════════════════════════════════════════════════════════════════
# Token Refresh
# ═══════════════════════════════════════════════════════════════════════

class TestRefresh:
    """POST /api/v1/auth/refresh"""

    @pytest.mark.smoke
    def test_refresh_success(self, api, test_user):
        """登录后刷新 token 返回新 access_token"""
        # 登录（同时保存 cookie）
        api.login(test_user["username"], test_user["password"])
        # 刷新
        resp = api.refresh()

        assert resp.status_code == 200
        data = resp.json()
        assert "access_token" in data
        assert data["token_type"] == "Bearer"
        assert "expires_in" in data

    def test_refresh_without_cookie(self, api):
        """无 Cookie 刷新返回 401"""
        # 确保没有残留 cookie
        api.session.cookies.clear()
        resp = api.refresh()
        assert resp.status_code == 401
        assert resp.json()["error"] == "no refresh token"

    def test_refresh_after_logout(self, api, test_user):
        """登出后刷新返回 401"""
        api.login(test_user["username"], test_user["password"])
        api.logout()
        resp = api.refresh()
        assert resp.status_code == 401
        error = resp.json()["error"]
        # curl 会清除 cookie，server 返回 "no refresh token"；若 cookie 仍在则 "refresh token revoked"
        assert error in ("no refresh token", "refresh token revoked")


# ═══════════════════════════════════════════════════════════════════════
# Logout
# ═══════════════════════════════════════════════════════════════════════

class TestLogout:
    """POST /api/v1/auth/logout"""

    @pytest.mark.smoke
    def test_logout_success(self, api, test_user):
        """正常登出返回 200"""
        api.login(test_user["username"], test_user["password"])
        resp = api.logout()

        assert resp.status_code == 200
        assert resp.json()["message"] == "logged out"

    def test_logout_without_cookie_idempotent(self, api):
        """无 Cookie 登出也返回 200（幂等）"""
        api.session.cookies.clear()
        resp = api.logout()
        assert resp.status_code == 200
