"""
管理后台 API 测试 — /api/v1/admin/*
覆盖: 鉴权检查、题目 CRUD、测试用例 CRUD、用户管理、统计数据
"""

import pytest


# ═══════════════════════════════════════════════════════════════════════
# Authorization Checks
# ═══════════════════════════════════════════════════════════════════════

class TestAdminAuth:
    """Admin 鉴权验证"""

    def test_non_admin_access_denied(self, authed_api):
        """非 admin 用户访问 admin 接口返回 403"""
        resp = authed_api.get("/admin/users")
        assert resp.status_code == 403
        assert resp.json()["error"] == "admin privileges required"

    def test_no_auth_access_denied(self, api):
        """无认证访问 admin 接口返回 401"""
        resp = api.get("/admin/users")
        assert resp.status_code == 401
        assert resp.json()["error"] == "authentication required"


# ═══════════════════════════════════════════════════════════════════════
# Problem CRUD
# ═══════════════════════════════════════════════════════════════════════

class TestAdminProblemCRUD:
    """POST/PUT/DELETE /api/v1/admin/problems[/:id]"""

    @pytest.mark.smoke
    def test_create_problem(self, admin_api):
        """创建题目返回 201，含 created_by 和 created_at"""
        resp = admin_api.post("/admin/problems", json={
            "title": "Pytest 测试题目",
            "description": "这是一道测试题目。",
            "difficulty": "easy",
            "time_limit_ms": 2000,
            "memory_limit_kb": 131072,
        })

        assert resp.status_code == 201
        data = resp.json()
        assert data["message"] == "ok"
        p = data["data"]
        assert p["title"] == "Pytest 测试题目"
        assert p["difficulty"] == "easy"
        assert "created_by" in p
        assert "created_at" in p
        assert p["id"] > 0

        # 清理
        admin_api.delete(f"/admin/problems/{p['id']}")

    def test_create_problem_with_testcases(self, admin_api):
        """创建题目时可附带测试用例"""
        resp = admin_api.post("/admin/problems", json={
            "title": "带用例的题目",
            "description": "含测试用例的题目。",
            "difficulty": "medium",
            "test_cases": [
                {"input": "1 2", "expected_output": "3", "is_sample": True, "order_index": 1},
                {"input": "5 5", "expected_output": "10", "is_sample": False, "order_index": 2},
            ],
        })

        assert resp.status_code == 201
        p = resp.json()["data"]
        assert len(p["test_cases"]) == 2
        assert p["test_cases"][0]["is_sample"] is True

        # 清理
        admin_api.delete(f"/admin/problems/{p['id']}")

    def test_create_problem_missing_title(self, admin_api):
        """缺少 title 返回 400"""
        resp = admin_api.post("/admin/problems", json={
            "description": "no title",
        })
        assert resp.status_code == 400
        assert resp.json()["error"] == "title is required"

    def test_create_problem_invalid_difficulty(self, admin_api):
        """非法 difficulty 返回 400"""
        resp = admin_api.post("/admin/problems", json={
            "title": "X", "description": "Y", "difficulty": "superhard",
        })
        assert resp.status_code == 400
        assert "difficulty" in resp.json()["error"]

    def test_update_problem(self, admin_api):
        """编辑题目返回 200，字段正确更新"""
        # 先创建
        resp = admin_api.post("/admin/problems", json={
            "title": "原始标题", "description": "原始描述",
        })
        pid = resp.json()["data"]["id"]

        # 更新
        resp = admin_api.put(f"/admin/problems/{pid}", json={
            "title": "修改后的标题",
            "description": "修改后的描述",
            "difficulty": "hard",
            "time_limit_ms": 5000,
            "memory_limit_kb": 524288,
        })

        assert resp.status_code == 200
        p = resp.json()["data"]
        assert p["title"] == "修改后的标题"
        assert p["difficulty"] == "hard"

        # 清理
        admin_api.delete(f"/admin/problems/{pid}")

    def test_update_nonexistent_problem(self, admin_api):
        """编辑不存在的题目返回 404"""
        resp = admin_api.put("/admin/problems/99999", json={
            "title": "X", "description": "Y",
        })
        assert resp.status_code == 404

    @pytest.mark.smoke
    def test_delete_problem(self, admin_api):
        """删除题目返回 200，含 deleted id"""
        # 先创建
        resp = admin_api.post("/admin/problems", json={
            "title": "待删除题目", "description": "将被删除。",
        })
        pid = resp.json()["data"]["id"]

        # 删除
        resp = admin_api.delete(f"/admin/problems/{pid}")
        assert resp.status_code == 200
        assert resp.json()["data"]["deleted"] == pid

        # 确认已删除
        resp = admin_api.get(f"/problems/{pid}")
        assert resp.status_code == 404

    def test_delete_nonexistent_problem(self, admin_api):
        """删除不存在的题目返回 404"""
        resp = admin_api.delete("/admin/problems/99999")
        assert resp.status_code == 404


# ═══════════════════════════════════════════════════════════════════════
# TestCase CRUD
# ═══════════════════════════════════════════════════════════════════════

class TestAdminTestCaseCRUD:
    """POST/PUT/DELETE /api/v1/admin/problems/:id/testcases, /admin/testcases/:id"""

    @pytest.fixture
    def test_problem_id(self, admin_api):
        """创建一个用于测试用例 CRUD 的题目，测试结束后清理。"""
        resp = admin_api.post("/admin/problems", json={
            "title": "TestCase CRUD 测试题目",
            "description": "用于测试用例 CRUD 的题目。",
        })
        pid = resp.json()["data"]["id"]
        yield pid
        admin_api.delete(f"/admin/problems/{pid}")

    @pytest.mark.smoke
    def test_add_testcase(self, admin_api, test_problem_id):
        """添加测试用例返回 201"""
        resp = admin_api.post(f"/admin/problems/{test_problem_id}/testcases", json={
            "input": "10 20",
            "expected_output": "30",
            "is_sample": True,
            "order_index": 1,
        })

        assert resp.status_code == 201
        tc = resp.json()["data"]
        assert tc["input"] == "10 20"
        assert tc["expected_output"] == "30"
        assert tc["is_sample"] is True
        assert tc["problem_id"] == test_problem_id
        assert tc["id"] > 0

        # 清理
        admin_api.delete(f"/admin/testcases/{tc['id']}")

    def test_add_testcase_missing_input(self, admin_api, test_problem_id):
        """缺少 input 返回 400"""
        resp = admin_api.post(f"/admin/problems/{test_problem_id}/testcases", json={
            "expected_output": "x",
        })
        assert resp.status_code == 400
        assert resp.json()["error"] == "input is required"

    def test_add_testcase_nonexistent_problem(self, admin_api):
        """向不存在的题目添加用例返回 404"""
        resp = admin_api.post("/admin/problems/99999/testcases", json={
            "input": "x", "expected_output": "y",
        })
        assert resp.status_code == 404

    def test_update_testcase(self, admin_api, test_problem_id):
        """编辑测试用例返回 200，字段部分更新"""
        # 创建
        resp = admin_api.post(f"/admin/problems/{test_problem_id}/testcases", json={
            "input": "old input",
            "expected_output": "old output",
            "is_sample": False,
            "order_index": 1,
        })
        tc_id = resp.json()["data"]["id"]

        # 更新
        resp = admin_api.put(f"/admin/testcases/{tc_id}", json={
            "problem_id": test_problem_id,
            "input": "new input",
            "expected_output": "new output",
        })

        assert resp.status_code == 200
        tc = resp.json()["data"]
        assert tc["input"] == "new input"
        assert tc["expected_output"] == "new output"

        # 清理
        admin_api.delete(f"/admin/testcases/{tc_id}")

    def test_update_nonexistent_testcase(self, admin_api, test_problem_id):
        """编辑不存在的测试用例返回 404"""
        resp = admin_api.put("/admin/testcases/99999", json={
            "problem_id": test_problem_id,
            "input": "x",
        })
        assert resp.status_code == 404

    def test_delete_testcase(self, admin_api, test_problem_id):
        """删除测试用例返回 200"""
        # 创建
        resp = admin_api.post(f"/admin/problems/{test_problem_id}/testcases", json={
            "input": "del test",
            "expected_output": "result",
        })
        tc_id = resp.json()["data"]["id"]

        # 删除
        resp = admin_api.delete(f"/admin/testcases/{tc_id}")
        assert resp.status_code == 200
        assert resp.json()["data"]["deleted"] == tc_id

    def test_delete_nonexistent_testcase(self, admin_api):
        """删除不存在的测试用例返回 404"""
        resp = admin_api.delete("/admin/testcases/99999")
        assert resp.status_code == 404


# ═══════════════════════════════════════════════════════════════════════
# User Management
# ═══════════════════════════════════════════════════════════════════════

class TestAdminUsers:
    """GET /api/v1/admin/users, PUT /admin/users/:id"""

    @pytest.mark.smoke
    def test_list_users(self, admin_api):
        """获取用户列表返回 200，含标准字段"""
        resp = admin_api.get("/admin/users")

        assert resp.status_code == 200
        data = resp.json()["data"]
        assert "items" in data
        assert "total" in data
        assert data["total"] >= 1

        if data["items"]:
            user = data["items"][0]
            assert "id" in user
            assert "username" in user
            assert "role" in user
            assert "status" in user
            assert "created_at" in user
            # 不应暴露 password_hash
            assert "password_hash" not in user

    def test_change_user_role(self, admin_api, test_user):
        """修改用户角色返回 200"""
        resp = admin_api.put(f"/admin/users/{test_user['user_id']}", json={
            "role": "admin",
        })

        assert resp.status_code == 200
        assert resp.json()["data"]["role"] == "admin"

        # 恢复
        admin_api.put(f"/admin/users/{test_user['user_id']}", json={
            "role": "user",
        })

    def test_disable_user(self, admin_api, test_user):
        """禁用用户后登录返回 403"""
        # 禁用
        resp = admin_api.put(f"/admin/users/{test_user['user_id']}", json={
            "status": "disabled",
        })
        assert resp.status_code == 200
        assert resp.json()["data"]["status"] == "disabled"

        # 验证无法登录
        api_client = type(admin_api)(admin_api.base_url)
        resp = api_client.login(test_user["username"], test_user["password"])
        assert resp.status_code == 403
        assert resp.json()["error"] == "account is disabled"

        # 恢复
        admin_api.put(f"/admin/users/{test_user['user_id']}", json={
            "status": "active",
        })

    def test_invalid_role(self, admin_api, test_user):
        """非法 role 返回 400"""
        resp = admin_api.put(f"/admin/users/{test_user['user_id']}", json={
            "role": "superadmin",
        })
        assert resp.status_code == 400
        assert "invalid role" in resp.json()["error"]

    def test_invalid_status(self, admin_api, test_user):
        """非法 status 返回 400"""
        resp = admin_api.put(f"/admin/users/{test_user['user_id']}", json={
            "status": "banned",
        })
        assert resp.status_code == 400
        assert "invalid status" in resp.json()["error"]

    def test_missing_role_and_status(self, admin_api, test_user):
        """缺少 role 和 status 返回 400"""
        resp = admin_api.put(f"/admin/users/{test_user['user_id']}", json={})
        assert resp.status_code == 400

    def test_update_nonexistent_user(self, admin_api):
        """更新不存在的用户返回 404"""
        resp = admin_api.put("/admin/users/99999", json={"role": "user"})
        assert resp.status_code == 404
        assert resp.json()["error"] == "user not found"


# ═══════════════════════════════════════════════════════════════════════
# Statistics
# ═══════════════════════════════════════════════════════════════════════

class TestAdminStats:
    """GET /api/v1/admin/stats"""

    @pytest.mark.smoke
    def test_get_stats(self, admin_api):
        """获取统计数据返回 200，含所有统计字段"""
        resp = admin_api.get("/admin/stats")

        assert resp.status_code == 200
        data = resp.json()["data"]
        assert "total_users" in data
        assert "total_problems" in data
        assert "total_submissions" in data
        assert "submissions_by_status" in data
        assert "daily_trend" in data
        assert isinstance(data["total_users"], int)
        assert isinstance(data["total_problems"], int)
        assert isinstance(data["total_submissions"], int)
        assert isinstance(data["submissions_by_status"], dict)
        assert isinstance(data["daily_trend"], list)

    def test_stats_without_auth(self, api):
        """无认证访问返回 401"""
        resp = api.get("/admin/stats")
        assert resp.status_code == 401
