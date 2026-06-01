"""
健康检查 API 测试 — GET /api/v1/health
"""

import pytest


class TestHealth:
    """GET /api/v1/health — 服务存活探测"""

    @pytest.mark.smoke
    def test_health_returns_ok(self, api):
        """验证健康检查返回 200 和 status=ok"""
        resp = api.get("/health")

        assert resp.status_code == 200, f"Expected 200, got {resp.status_code}"
        data = resp.json()
        assert data["status"] == "ok", f"Expected status=ok, got {data}"
