"""
题目 API 测试 — GET /api/v1/problems, /api/v1/problems/:id
"""

import pytest


# ═══════════════════════════════════════════════════════════════════════
# Problem List
# ═══════════════════════════════════════════════════════════════════════

class TestProblemList:
    """GET /api/v1/problems"""

    @pytest.mark.smoke
    def test_list_all_problems(self, api):
        """获取全部题目列表返回 200，含标准分页结构"""
        resp = api.get("/problems")

        assert resp.status_code == 200
        data = resp.json()
        assert data["message"] == "ok"
        assert "items" in data["data"]
        assert "total" in data["data"]
        assert data["data"]["page"] == 1
        assert data["data"]["page_size"] == 20

    def test_list_items_have_required_fields(self, api):
        """列表项只包含 id/title/difficulty/created_at"""
        resp = api.get("/problems")
        items = resp.json()["data"]["items"]
        if items:
            item = items[0]
            assert "id" in item
            assert "title" in item
            assert "difficulty" in item
            assert "created_at" in item
            # 列表不应包含 description
            assert "description" not in item

    def test_filter_by_difficulty(self, api):
        """按难度筛选"""
        resp = api.get("/problems", params={"difficulty": "easy"})
        assert resp.status_code == 200
        items = resp.json()["data"]["items"]
        for item in items:
            assert item["difficulty"] == "easy"

    def test_invalid_difficulty(self, api):
        """非法难度值返回 400"""
        resp = api.get("/problems", params={"difficulty": "impossible"})
        assert resp.status_code == 400
        assert "invalid difficulty" in resp.json()["error"]

    def test_pagination(self, api):
        """分页参数回显正确"""
        resp = api.get("/problems", params={"page": 1, "page_size": 2})
        assert resp.status_code == 200
        data = resp.json()["data"]
        assert data["page"] == 1
        assert data["page_size"] == 2

    def test_page_out_of_range(self, api):
        """超出范围页码返回空列表"""
        resp = api.get("/problems", params={"page": 99999})
        assert resp.status_code == 200
        assert resp.json()["data"]["items"] == []

    def test_page_size_capped(self, api):
        """page_size 超过 100 应被截断为 100"""
        resp = api.get("/problems", params={"page_size": 200})
        assert resp.status_code == 200
        assert resp.json()["data"]["page_size"] <= 100

    def test_non_numeric_pagination(self, api):
        """非数字分页参数容错处理"""
        resp = api.get("/problems", params={"page": "abc", "page_size": "xyz"})
        assert resp.status_code == 200


# ═══════════════════════════════════════════════════════════════════════
# Problem Detail
# ═══════════════════════════════════════════════════════════════════════

class TestProblemDetail:
    """GET /api/v1/problems/:id"""

    @pytest.mark.smoke
    def test_get_problem_detail(self, api, known_problem_id):
        """获取题目详情返回完整信息 + 样例测试用例"""
        resp = api.get(f"/problems/{known_problem_id}")

        assert resp.status_code == 200
        data = resp.json()
        assert data["message"] == "ok"
        p = data["data"]
        assert p["id"] == known_problem_id
        assert "title" in p
        assert "description" in p
        assert "difficulty" in p
        assert "time_limit_ms" in p
        assert "memory_limit_kb" in p
        assert "created_at" in p
        assert "sample_cases" in p
        # sample_cases 只应包含 is_sample=true 的用例
        for case in p["sample_cases"]:
            assert "input" in case
            assert "expected_output" in case

    def test_problem_not_found(self, api):
        """不存在的题目返回 404"""
        resp = api.get("/problems/99999")
        assert resp.status_code == 404
        assert resp.json()["error"] == "problem not found"
