"""
提交与判题 API 测试 — POST /api/v1/submissions, GET /submissions, GET /submissions/:id
"""

import pytest


# C++ 正确代码（两数之和）
AC_CODE = """#include <iostream>
using namespace std;
int main() {
    int n; cin >> n;
    int a[n];
    for(int i=0;i<n;i++) cin >> a[i];
    int t; cin >> t;
    for(int i=0;i<n;i++)
        for(int j=i+1;j<n;j++)
            if(a[i]+a[j]==t) {
                cout << i << " " << j << endl;
                return 0;
            }
    return 0;
}"""


# ═══════════════════════════════════════════════════════════════════════
# Create Submission
# ═══════════════════════════════════════════════════════════════════════

class TestCreateSubmission:
    """POST /api/v1/submissions"""

    @pytest.mark.smoke
    def test_submit_code_success(self, authed_api, known_problem_id):
        """提交代码成功返回 201 和 submission_id"""
        resp = authed_api.post("/submissions", json={
            "problem_id": known_problem_id,
            "code": AC_CODE,
        })

        assert resp.status_code == 201
        data = resp.json()
        assert data["message"] == "ok"
        assert "submission_id" in data["data"]
        assert isinstance(data["data"]["submission_id"], int)

    def test_submit_without_auth(self, api, known_problem_id):
        """未认证提交返回 401"""
        resp = api.post("/submissions", json={
            "problem_id": known_problem_id,
            "code": "int main(){}",
        })
        assert resp.status_code == 401
        assert resp.json()["error"] == "authentication required"

    def test_submit_missing_problem_id(self, authed_api):
        """缺少 problem_id 返回 400"""
        resp = authed_api.post("/submissions", json={
            "code": "int main(){}",
        })
        assert resp.status_code == 400
        assert "problem_id" in resp.json()["error"]

    def test_submit_empty_code(self, authed_api, known_problem_id):
        """空代码返回 400"""
        resp = authed_api.post("/submissions", json={
            "problem_id": known_problem_id,
            "code": "",
        })
        assert resp.status_code == 400
        assert "code" in resp.json()["error"]

    def test_submit_nonexistent_problem(self, authed_api):
        """不存在的题目返回 404"""
        resp = authed_api.post("/submissions", json={
            "problem_id": 99999,
            "code": "int main(){}",
        })
        assert resp.status_code == 404


# ═══════════════════════════════════════════════════════════════════════
# Submission Detail
# ═══════════════════════════════════════════════════════════════════════

class TestSubmissionDetail:
    """GET /api/v1/submissions/:id"""

    @pytest.mark.smoke
    def test_get_submission_detail(self, authed_api, known_problem_id):
        """查询提交详情返回完整字段"""
        # 先提交
        resp = authed_api.post("/submissions", json={
            "problem_id": known_problem_id,
            "code": AC_CODE,
        })
        sub_id = resp.json()["data"]["submission_id"]

        # 等待判题
        import time
        time.sleep(2)

        # 查询
        resp = authed_api.get(f"/submissions/{sub_id}")
        assert resp.status_code == 200
        data = resp.json()
        assert data["message"] == "ok"
        sub = data["data"]
        assert sub["id"] == sub_id
        assert "user_id" in sub
        assert "problem_id" in sub
        assert "code" in sub
        assert "status" in sub
        assert "created_at" in sub
        assert "updated_at" in sub

    def test_detail_without_auth(self, api):
        """未认证查询返回 401"""
        resp = api.get("/submissions/1")
        assert resp.status_code == 401

    def test_detail_not_found(self, authed_api):
        """不存在的提交返回 404"""
        resp = authed_api.get("/submissions/99999")
        assert resp.status_code == 404


# ═══════════════════════════════════════════════════════════════════════
# Submission List
# ═══════════════════════════════════════════════════════════════════════

class TestSubmissionList:
    """GET /api/v1/submissions"""

    @pytest.mark.smoke
    def test_list_submissions(self, authed_api, known_problem_id):
        """获取提交列表返回分页结构"""
        # 确保至少有一条提交
        authed_api.post("/submissions", json={
            "problem_id": known_problem_id,
            "code": AC_CODE,
        })

        resp = authed_api.get("/submissions")
        assert resp.status_code == 200
        data = resp.json()
        assert data["message"] == "ok"
        assert "items" in data["data"]
        assert "total" in data["data"]
        assert data["data"]["total"] >= 1

    def test_filter_by_problem_id(self, authed_api, known_problem_id):
        """按 problem_id 筛选"""
        resp = authed_api.get("/submissions",
                              params={"problem_id": known_problem_id})
        assert resp.status_code == 200

    def test_invalid_user_id(self, authed_api):
        """非法 user_id 返回 400"""
        resp = authed_api.get("/submissions", params={"user_id": "abc"})
        assert resp.status_code == 400

    def test_list_without_auth(self, api):
        """未认证返回 401"""
        resp = api.get("/submissions")
        assert resp.status_code == 401

    def test_regular_user_only_sees_own(self, authed_api, test_user, known_problem_id):
        """普通用户只能看到自己的提交"""
        # 提交一条
        authed_api.post("/submissions", json={
            "problem_id": known_problem_id,
            "code": AC_CODE,
        })
        # 查询（指定其他 user_id 应被忽略）
        resp = authed_api.get("/submissions", params={"user_id": 99999})
        assert resp.status_code == 200
        items = resp.json()["data"]["items"]
        for item in items:
            assert item["user_id"] == test_user["user_id"], \
                f"Expected all submissions to belong to user {test_user['user_id']}"

    def test_admin_can_see_others(self, admin_api, test_user):
        """Admin 可以通过 user_id 查看他人提交"""
        resp = admin_api.get("/submissions",
                             params={"user_id": test_user["user_id"]})
        assert resp.status_code == 200
