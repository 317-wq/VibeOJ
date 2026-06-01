"""
pytest 共享 fixtures — 为所有测试模块提供：
- API 客户端 (自动管理 Cookie 和 Auth)
- 测试用户 (自动注册/登录/清理)
- Admin 用户 (自动创建或提升)
- 已知题目 ID (用于提交测试)
"""

import os
import subprocess
import time

import pytest

from utils.api_client import ApiClient

# ═══════════════════════════════════════════════════════════════════════
# 配置常量
# ═══════════════════════════════════════════════════════════════════════

BASE_URL = os.environ.get("API_BASE_URL", "http://localhost:8080/api/v1")
MYSQL_PWD = os.environ.get("MYSQL_PWD", "lijiatong344A@")

# 用于清理的全局记录
_created_users = []
_created_problems = []


def _mysql_exec(sql: str) -> str:
    """执行 MySQL 查询并返回 stdout。"""
    result = subprocess.run(
        ["mysql", "-uljt", "oj_system", "-e", sql],
        capture_output=True, text=True,
        env={**os.environ, "MYSQL_PWD": MYSQL_PWD},
    )
    return result.stdout


# ═══════════════════════════════════════════════════════════════════════
# Session-scoped fixtures (整个测试会话共享)
# ═══════════════════════════════════════════════════════════════════════

@pytest.fixture(scope="session")
def api() -> ApiClient:
    """返回一个未认证的 API 客户端实例。"""
    return ApiClient(BASE_URL)


@pytest.fixture(scope="session")
def test_user(api: ApiClient) -> dict:
    """在会话开始时注册一个唯一测试用户，结束时清理。

    Returns:
        {"username": str, "password": str, "user_id": int}
    """
    ts = int(time.time())
    username = f"pytest_{ts}"
    password = "pytest123"

    resp = api.register(username, password)
    if resp.status_code == 201:
        user_id = resp.json()["id"]
    elif resp.status_code == 409:
        # 用户已存在，通过 SQL 获取 ID
        out = _mysql_exec(
            f"SELECT id FROM users WHERE username='{username}'"
        )
        user_id = int(out.strip().split("\n")[-1])
    else:
        pytest.fail(f"Failed to create test user: {resp.status_code} {resp.text}")

    _created_users.append(username)
    yield {"username": username, "password": password, "user_id": user_id}

    # 清理：删除测试用户
    _mysql_exec(f"DELETE FROM users WHERE username='{username}'")


@pytest.fixture(scope="session")
def user_token(api: ApiClient, test_user: dict) -> str:
    """以 test_user 登录，返回 access_token。"""
    resp = api.login(test_user["username"], test_user["password"])
    assert resp.status_code == 200, f"User login failed: {resp.text}"
    return resp.json()["access_token"]


@pytest.fixture(scope="session")
def admin_token(api: ApiClient) -> str:
    """确保 admin 用户存在并返回其 access_token。

    优先使用 tester1（已有），否则创建新 admin。
    """
    admin_user = "tester1"
    admin_pass = "adminpass123"

    # 确保 tester1 是 admin
    _mysql_exec(
        f"UPDATE users SET role='admin', status='active' WHERE username='{admin_user}'"
    )

    # 尝试登录
    resp = api.login(admin_user, admin_pass)
    if resp.status_code != 200:
        # tester1 密码不对，创建新 admin
        ts = int(time.time())
        admin_user = f"pytest_admin_{ts}"
        resp = api.register(admin_user, admin_pass)
        if resp.status_code == 201 or resp.status_code == 409:
            _mysql_exec(
                f"UPDATE users SET role='admin', status='active' "
                f"WHERE username='{admin_user}'"
            )
        resp = api.login(admin_user, admin_pass)
        if resp.status_code != 200:
            pytest.fail(f"Cannot create/login admin user: {resp.text}")
        _created_users.append(admin_user)

    token = resp.json()["access_token"]
    assert resp.json()["user"]["role"] == "admin", "Admin user must have admin role"
    yield token

    # 清理：恢复 tester1 角色，删除临时 admin
    _mysql_exec("UPDATE users SET role='user' WHERE username='tester1'")
    if admin_user.startswith("pytest_admin_"):
        _mysql_exec(f"DELETE FROM users WHERE username='{admin_user}'")


@pytest.fixture(scope="session")
def known_problem_id(api: ApiClient) -> int:
    """确保至少有一个题目存在，返回其 ID。

    种子数据会在服务器启动时自动导入，此 fixture 获取第一个题目 ID。
    """
    resp = api.get("/problems")
    if resp.status_code != 200:
        pytest.fail(f"Cannot fetch problems: {resp.text}")

    items = resp.json()["data"]["items"]
    if not items:
        # 种子数据未导入，通过 admin 创建题目
        pytest.fail(
            "No problems found. Ensure server started with seed data import."
        )
    return items[0]["id"]


# ═══════════════════════════════════════════════════════════════════════
# Function-scoped fixtures (每个测试函数独立)
# ═══════════════════════════════════════════════════════════════════════

@pytest.fixture
def authed_api(api: ApiClient, user_token: str) -> ApiClient:
    """返回已设置普通用户认证的客户端（每个测试独立副本）。"""
    client = ApiClient(BASE_URL)
    client.set_auth(user_token)
    return client


@pytest.fixture
def admin_api(api: ApiClient, admin_token: str) -> ApiClient:
    """返回已设置 admin 认证的客户端（每个测试独立副本）。"""
    client = ApiClient(BASE_URL)
    client.set_auth(admin_token)
    return client
