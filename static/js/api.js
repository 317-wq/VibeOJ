/* ==========================================================================
 * VibeOJ — API 模块
 * Fetch 封装 + Token 管理 + 自动 Refresh
 *
 * 用法：
 *   import { api } from './api.js';
 *   const problems = await api.getProblems({ difficulty: 'easy' });
 * ========================================================================== */

const API = (function () {
  'use strict';

  /* ═══════════════════════════════════════════════════════════════════════
   * 配置
   * ═══════════════════════════════════════════════════════════════════════ */

  const BASE_URL = '/api/v1';
  const LOGIN_PAGE = '/login.html';

  /* ═══════════════════════════════════════════════════════════════════════
   * 内部状态 (闭包私有)
   * ═══════════════════════════════════════════════════════════════════════ */

  let accessToken = null;       // 当前有效的 access_token
  let refreshPromise = null;    // 正在进行的 refresh 请求 (防并发)
  let onAuthChange = null;      // 认证状态变化回调

  /* ═══════════════════════════════════════════════════════════════════════
   * Token 管理
   * ═══════════════════════════════════════════════════════════════════════ */

  /**
   * 设置 access token。
   * 登录成功或 refresh 成功后调用。
   */
  function setAuth(token) {
    accessToken = token;
    if (onAuthChange) onAuthChange(!!token);
  }

  /**
   * 清除认证状态。
   * 登出或 refresh 失败后调用。
   */
  function clearAuth() {
    accessToken = null;
    if (onAuthChange) onAuthChange(false);
  }

  /**
   * 获取当前 access token。
   */
  function getAuth() {
    return accessToken;
  }

  /**
   * 是否已认证。
   */
  function isAuthenticated() {
    return !!accessToken;
  }

  /**
   * 注册认证状态变化回调。
   * 页面可用此回调更新导航栏 (登录/登出按钮切换)。
   */
  function onAuthChangeEvent(cb) {
    onAuthChange = cb;
  }

  /* ═══════════════════════════════════════════════════════════════════════
   * HTTP 请求核心
   * ═══════════════════════════════════════════════════════════════════════ */

  /**
   * 发送 API 请求。
   *
   * 功能：
   * - 自动添加 Content-Type: application/json
   * - 自动添加 Authorization: Bearer header (当 token 存在且请求需要认证)
   * - 收到 401 时自动尝试 refresh token，成功后重放原请求
   * - refresh 失败时清除 token 并跳转登录页
   * - 自动解析 JSON 响应体
   *
   * @param {string} method  - HTTP 方法 (GET/POST/PUT/DELETE)
   * @param {string} path    - API 路径 (不含 base URL)，例如 '/problems'
   * @param {object} [body]  - 请求体 (仅 POST/PUT)，传 null 则无 body
   * @param {object} [opts]  - 可选参数
   *   {boolean} auth        - 是否需要 Bearer Token (默认 false)
   *   {object}  params      - URL 查询参数
   *   {boolean} raw         - 返回原始 Response (默认 false，自动 parse JSON)
   * @returns {Promise<object>} { status, data, message?, error? }
   */
  async function request(method, path, body, opts) {
    const { auth, params, raw } = Object.assign(
      { auth: false, params: null, raw: false },
      opts || {}
    );

    // 构建 URL
    let url = BASE_URL + path;
    if (params) {
      const qs = new URLSearchParams();
      for (const [k, v] of Object.entries(params)) {
        if (v !== undefined && v !== null && v !== '') {
          qs.set(k, v);
        }
      }
      const qsStr = qs.toString();
      if (qsStr) url += '?' + qsStr;
    }

    // 构建 headers
    const headers = {};

    // 有 body 时自动设 Content-Type (FormData 除外)
    if (body !== null && body !== undefined && !(body instanceof FormData)) {
      headers['Content-Type'] = 'application/json';
    }

    // 需要认证时添加 Bearer Token
    if (auth && accessToken) {
      headers['Authorization'] = 'Bearer ' + accessToken;
    }

    // 构建 fetch options
    const fetchOpts = { method, headers };
    if (body !== null && body !== undefined) {
      fetchOpts.body = (body instanceof FormData) ? body : JSON.stringify(body);
    }
    // 跨域请求携带 Cookie (用于 refresh_token)
    fetchOpts.credentials = 'same-origin';

    let resp = await fetch(url, fetchOpts);

    // 401 且需要认证 → 尝试 refresh
    if (resp.status === 401 && auth) {
      const refreshed = await tryRefresh();
      if (refreshed) {
        // 更新 token 后重放原请求
        headers['Authorization'] = 'Bearer ' + accessToken;
        resp = await fetch(url, { ...fetchOpts, headers });
      } else {
        // refresh 失败 → 跳转登录
        redirectToLogin();
        throw new ApiError(401, 'authentication required');
      }
    }

    if (raw) return resp;

    // 解析 JSON
    let json;
    try {
      json = await resp.json();
    } catch (_) {
      throw new ApiError(resp.status, 'invalid JSON response');
    }

    // 返回统一结构: { status, data?, message?, error? }
    return {
      status: resp.status,
      data: json.data,
      message: json.message,
      error: json.error,
    };
  }

  /* ═══════════════════════════════════════════════════════════════════════
   * Token Refresh
   * ═══════════════════════════════════════════════════════════════════════ */

  /**
   * 尝试用 httpOnly Cookie 中的 refresh_token 换取新 access_token。
   * 内置防并发保护: 多个 401 请求同时到达时只发送一次 refresh。
   *
   * @returns {Promise<boolean>} 成功返回 true，失败返回 false
   */
  async function tryRefresh() {
    // 已有正在进行的 refresh，复用其结果
    if (refreshPromise) {
      return refreshPromise;
    }

    refreshPromise = (async () => {
      try {
        const resp = await fetch(BASE_URL + '/auth/refresh', {
          method: 'POST',
          credentials: 'same-origin',  // 携带 Cookie
        });

        if (resp.status !== 200) {
          clearAuth();
          return false;
        }

        const json = await resp.json();
        const newAccessToken = json.access_token;
        if (newAccessToken) {
          setAuth(newAccessToken);
          return true;
        }

        clearAuth();
        return false;
      } catch (_) {
        clearAuth();
        return false;
      } finally {
        refreshPromise = null;
      }
    })();

    return refreshPromise;
  }

  /**
   * 跳转到登录页。
   */
  function redirectToLogin() {
    if (window.location.pathname !== LOGIN_PAGE) {
      window.location.href = LOGIN_PAGE + '?redirect=' + encodeURIComponent(window.location.href);
    }
  }

  /* ═══════════════════════════════════════════════════════════════════════
   * 自定义错误类
   * ═══════════════════════════════════════════════════════════════════════ */

  class ApiError extends Error {
    constructor(status, message) {
      super(message);
      this.name = 'ApiError';
      this.status = status;
    }
  }

  /* ═══════════════════════════════════════════════════════════════════════
   * 公开 API — 认证
   * ═══════════════════════════════════════════════════════════════════════ */

  /**
   * 用户注册。
   * 注意: 认证端点不使用标准 {data,message} 信封。
   * @param {string} username
   * @param {string} password
   * @returns {Promise<object>} { status, data: { id, username, role }, error? }
   */
  async function register(username, password) {
    const resp = await fetch(BASE_URL + '/auth/register', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username, password }),
      credentials: 'same-origin',
    });
    const json = await resp.json();
    return { status: resp.status, data: resp.ok ? json : null, error: json.error };
  }

  /**
   * 用户登录。
   * 成功后自动存储 access_token。
   * Refresh token 由浏览器通过 Set-Cookie 自动保存 (httpOnly)。
   * @param {string} username
   * @param {string} password
   * @returns {Promise<object>} { status, data: { access_token, token_type, expires_in, user }, error? }
   */
  async function login(username, password) {
    const resp = await fetch(BASE_URL + '/auth/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username, password }),
      credentials: 'same-origin',
    });
    const json = await resp.json();
    if (resp.ok && json.access_token) {
      setAuth(json.access_token);
    }
    return { status: resp.status, data: resp.ok ? json : null, error: json.error };
  }

  /**
   * 登出。撤销 refresh_token 并清除本地 access_token。
   * @returns {Promise<object>}
   */
  async function logout() {
    const result = await request('POST', '/auth/logout', null);
    clearAuth();
    return result;
  }

  /**
   * 手动刷新 access token (通常由 onFocus 等事件调用)。
   * @returns {Promise<object>}
   */
  async function refreshToken() {
    const ok = await tryRefresh();
    return { status: ok ? 200 : 401, data: ok ? { access_token: accessToken } : null };
  }

  /* ═══════════════════════════════════════════════════════════════════════
   * 公开 API — 题目
   * ═══════════════════════════════════════════════════════════════════════ */

  /**
   * 获取题目列表 (分页 + 可选难度筛选)。
   * @param {object} [params] - { difficulty?, page?, page_size? }
   * @returns {Promise<object>}
   */
  async function getProblems(params) {
    return request('GET', '/problems', null, { params });
  }

  /**
   * 获取题目详情 (含描述 + 可见样例)。
   * @param {number} id - 题目 ID
   * @returns {Promise<object>}
   */
  async function getProblem(id) {
    return request('GET', '/problems/' + id);
  }

  /* ═══════════════════════════════════════════════════════════════════════
   * 公开 API — 提交与判题
   * ═══════════════════════════════════════════════════════════════════════ */

  /**
   * 提交代码进行判题。
   * @param {number} problemId
   * @param {string} code - C++ 源代码
   * @returns {Promise<object>} 成功: { submission_id }
   */
  async function submitCode(problemId, code) {
    return request('POST', '/submissions',
      { problem_id: problemId, code },
      { auth: true }
    );
  }

  /**
   * 查询单条提交详情。
   * @param {number} id - 提交 ID
   * @returns {Promise<object>}
   */
  async function getSubmission(id) {
    return request('GET', '/submissions/' + id, null, { auth: true });
  }

  /**
   * 获取提交列表 (分页 + 可选筛选)。
   * @param {object} [params] - { problem_id?, user_id?, page?, page_size? }
   * @returns {Promise<object>}
   */
  async function getSubmissions(params) {
    return request('GET', '/submissions', null, { auth: true, params });
  }

  /* ═══════════════════════════════════════════════════════════════════════
   * 公开 API — 管理后台 (admin only)
   * ═══════════════════════════════════════════════════════════════════════ */

  /**
   * 创建题目 (可同时附带测试用例)。
   * @param {object} data - { title, description, difficulty?, time_limit_ms?, memory_limit_kb?, test_cases? }
   * @returns {Promise<object>}
   */
  async function createProblem(data) {
    return request('POST', '/admin/problems', data, { auth: true });
  }

  /**
   * 编辑题目。
   * @param {number} id
   * @param {object} data - { title, description, difficulty?, time_limit_ms?, memory_limit_kb? }
   * @returns {Promise<object>}
   */
  async function updateProblem(id, data) {
    return request('PUT', '/admin/problems/' + id, data, { auth: true });
  }

  /**
   * 删除题目 (级联删除测试用例)。
   * @param {number} id
   * @returns {Promise<object>}
   */
  async function deleteProblem(id) {
    return request('DELETE', '/admin/problems/' + id, null, { auth: true });
  }

  /**
   * 为指定题目添加测试用例。
   * @param {number} problemId
   * @param {object} data - { input, expected_output, is_sample?, order_index? }
   * @returns {Promise<object>}
   */
  async function addTestCase(problemId, data) {
    return request('POST', '/admin/problems/' + problemId + '/testcases', data, { auth: true });
  }

  /**
   * 编辑测试用例。
   * @param {number} id
   * @param {object} data - { problem_id, input?, expected_output?, is_sample?, order_index? }
   * @returns {Promise<object>}
   */
  async function updateTestCase(id, data) {
    return request('PUT', '/admin/testcases/' + id, data, { auth: true });
  }

  /**
   * 删除测试用例。
   * @param {number} id
   * @returns {Promise<object>}
   */
  async function deleteTestCase(id) {
    return request('DELETE', '/admin/testcases/' + id, null, { auth: true });
  }

  /**
   * 获取用户列表 (无分页)。
   * @returns {Promise<object>}
   */
  async function getUsers() {
    return request('GET', '/admin/users', null, { auth: true });
  }

  /**
   * 修改用户角色/状态。
   * @param {number} id
   * @param {object} data - { role?, status? }
   * @returns {Promise<object>}
   */
  async function updateUser(id, data) {
    return request('PUT', '/admin/users/' + id, data, { auth: true });
  }

  /**
   * 获取平台统计数据。
   * @returns {Promise<object>}
   */
  async function getStats() {
    return request('GET', '/admin/stats', null, { auth: true });
  }

  /* ═══════════════════════════════════════════════════════════════════════
   * 公开 API — 健康检查
   * ═══════════════════════════════════════════════════════════════════════ */

  /**
   * 健康检查。
   * @returns {Promise<object>}
   */
  async function healthCheck() {
    return request('GET', '/health');
  }

  /* ═══════════════════════════════════════════════════════════════════════
   * 公开 API
   * ═══════════════════════════════════════════════════════════════════════ */

  return {
    // Token 管理
    setAuth,
    clearAuth,
    getAuth,
    isAuthenticated,
    onAuthChange: onAuthChangeEvent,

    // 认证
    register,
    login,
    logout,
    refreshToken,

    // 题目
    getProblems,
    getProblem,

    // 提交
    submitCode,
    getSubmission,
    getSubmissions,

    // 管理后台
    createProblem,
    updateProblem,
    deleteProblem,
    addTestCase,
    updateTestCase,
    deleteTestCase,
    getUsers,
    updateUser,
    getStats,

    // 健康检查
    healthCheck,

    // 底层工具 (供测试/高级用法)
    request,
    ApiError,
  };
})();

// 支持 ES module 和 <script> 引入
if (typeof module !== 'undefined' && module.exports) {
  module.exports = { api: API };
}
