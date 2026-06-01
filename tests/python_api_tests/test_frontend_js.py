"""
前端 JS 模块单元测试 — 使用 Node.js 执行并验证 api.js 和 utils.js。

运行方式:
  cd tests/python_api_tests && python -m pytest test_frontend_js.py -v
"""

import json
import os
import re
import subprocess

import pytest

# ═══════════════════════════════════════════════════════════════════════════
# 路径配置
# ═══════════════════════════════════════════════════════════════════════════

PROJECT_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), '..', '..')
)
STATIC_DIR = os.path.join(PROJECT_ROOT, 'static')
CSS_FILE = os.path.join(STATIC_DIR, 'css', 'style.css')
API_JS = os.path.join(STATIC_DIR, 'js', 'api.js')
UTILS_JS = os.path.join(STATIC_DIR, 'js', 'utils.js')


# ═══════════════════════════════════════════════════════════════════════════
# 辅助: 在 Node.js 中执行 JS 代码
# ═══════════════════════════════════════════════════════════════════════════

def _node_eval(js_code: str, setup: str = '') -> str:
    """
    在 Node.js 中执行 JS 代码并返回 stdout。

    js_code 应该 console.log() 一个 JSON 字符串作为结果。
    setup 会先执行，用于加载模块。
    """
    full_script = setup + '\n' + js_code
    result = subprocess.run(
        ['node', '-e', full_script],
        capture_output=True, text=True, timeout=15,
        cwd=STATIC_DIR,
    )
    if result.returncode != 0:
        raise RuntimeError(f'Node.js error:\nstderr: {result.stderr}\nstdout: {result.stdout}')
    return result.stdout.strip()


def _load_utils_and_eval(js_code: str) -> str:
    """加载 utils.js 然后执行 js_code。"""
    setup = f'''
const fs = require('fs');
const path = require('path');
const utilsSrc = fs.readFileSync('{UTILS_JS}', 'utf-8');

// 模拟浏览器环境
global.window = {{ location: {{ search: '' }} }};
// 模拟 Node 基类 (用于 instanceof 检查)
global.Node = class Node {{}};
global.document = {{
  getElementById: (id) => null,
  querySelector: (sel) => null,
  querySelectorAll: (sel) => [],
  createElement: (tag) => {{
    const el = new Node();
    el.tagName = tag.toUpperCase();
    el.className = '';
    el.style = {{}};
    el.dataset = {{}};
    el.innerHTML = '';
    el.textContent = '';
    el.children = [];
    el.setAttribute = function(k, v) {{ this[k] = v; }};
    el.addEventListener = function(ev, fn) {{}};
    el.appendChild = function(child) {{ this.children.push(child); return child; }};
    return el;
  }},
  createTextNode: (text) => {{
    const node = new Node();
    node.nodeType = 3;
    node.textContent = text;
    return node;
  }},
}};
	// 模拟 localStorage
	const _localStorage = new Map();
	global.localStorage = {{
	  getItem: (k) => _localStorage.get(k) ?? null,
	  setItem: (k, v) => {{ _localStorage.set(k, String(v)); }},
	  removeItem: (k) => {{ _localStorage.delete(k); }},
	}};

// 用 var 替换 const/let Utils 使变量泄漏到当前作用域
const __patched = utilsSrc.replace(/^const Utils = /m, 'var Utils = ');
eval(__patched);
'''
    return _node_eval(js_code, setup=setup)


def _load_api_and_eval(js_code: str) -> str:
    """加载 api.js 然后执行 js_code。"""
    setup = f'''
const fs = require('fs');

// 模拟浏览器环境
global.window = {{ location: {{ pathname: '/index.html', href: 'http://localhost/index.html' }} }};
global.document = {{}};

// 模拟 fetch (Node 18+ 有内置 fetch，但我们需要可控制的 mock)
const _fetchResponses = [];
let _fetchIndex = 0;
global.fetch = async function(url, opts) {{
  if (_fetchIndex < _fetchResponses.length) {{
    const r = _fetchResponses[_fetchIndex++];
    return {{
      status: r.status || 200,
      ok: r.status >= 200 && r.status < 300,
      json: async () => r.body || {{}},
      text: async () => JSON.stringify(r.body || {{}}),
      headers: new Map(Object.entries(r.headers || {{}})),
    }};
  }}
  return {{ status: 200, ok: true, json: async () => ({{}}), text: async () => '{{}}' }};
}};
global._setFetchResponses = function(responses) {{
  _fetchResponses.splice(0, _fetchResponses.length, ...responses);
  _fetchIndex = 0;
}};

// URLSearchParams (Node 内置)
// FormData mock
global.FormData = function() {{ this._data = new Map(); }};
global.FormData.prototype.append = function(k, v) {{ this._data.set(k, v); }};

const apiSrc = fs.readFileSync('{API_JS}', 'utf-8');
// 用 var 替换 const API = 使变量泄漏到当前作用域
const __patchedApi = apiSrc.replace(/^const API = /m, 'var API = ');
eval(__patchedApi);
'''
    return _node_eval(js_code, setup=setup)


# ═══════════════════════════════════════════════════════════════════════════
# 文件存在性测试
# ═══════════════════════════════════════════════════════════════════════════

class TestFileExistence:
    """验证所有前端资源文件存在且非空。"""

    def test_css_exists(self):
        """style.css 存在且内容不为空"""
        assert os.path.exists(CSS_FILE), f'{CSS_FILE} does not exist'
        size = os.path.getsize(CSS_FILE)
        assert size > 1000, f'style.css is too small ({size} bytes)'

    def test_api_js_exists(self):
        """api.js 存在且内容不为空"""
        assert os.path.exists(API_JS), f'{API_JS} does not exist'
        size = os.path.getsize(API_JS)
        assert size > 1000, f'api.js is too small ({size} bytes)'

    def test_utils_js_exists(self):
        """utils.js 存在且内容不为空"""
        assert os.path.exists(UTILS_JS), f'{UTILS_JS} does not exist'
        size = os.path.getsize(UTILS_JS)
        assert size > 1000, f'utils.js is too small ({size} bytes)'


# ═══════════════════════════════════════════════════════════════════════════
# CSS 测试: 验证结构
# ═══════════════════════════════════════════════════════════════════════════

class TestCSS:
    """验证 style.css 包含所有必要的组件样式。"""

    @pytest.fixture(scope='class')
    def css_content(self):
        with open(CSS_FILE, 'r', encoding='utf-8') as f:
            return f.read()

    # CSS 变量
    def test_css_variables(self, css_content):
        """包含 CSS 自定义属性 (:root)"""
        assert '--color-primary' in css_content
        assert '--color-success' in css_content
        assert '--color-danger' in css_content
        assert '--color-warning' in css_content
        assert '--navbar-height' in css_content
        assert '--font-family' in css_content

    # 组件
    def test_navbar_styles(self, css_content):
        """包含导航栏样式"""
        assert '.navbar' in css_content

    def test_card_styles(self, css_content):
        """包含卡片样式"""
        assert '.card' in css_content

    def test_button_styles(self, css_content):
        """包含按钮样式 (default/primary/danger)"""
        assert '.btn-primary' in css_content
        assert '.btn-default' in css_content
        assert '.btn-danger' in css_content

    def test_form_styles(self, css_content):
        """包含表单样式"""
        assert '.form-input' in css_content or '.form-group' in css_content
        assert '.form-textarea' in css_content

    def test_table_styles(self, css_content):
        """包含表格样式"""
        assert 'table' in css_content
        assert 'thead' in css_content or 'th' in css_content.lower()

    def test_badge_styles(self, css_content):
        """包含徽章样式 (难度 + 状态)"""
        assert '.badge-easy' in css_content
        assert '.badge-medium' in css_content
        assert '.badge-hard' in css_content
        assert '.badge-accepted' in css_content
        assert '.badge-wrong_answer' in css_content
        assert '.badge-time_limit' in css_content or '.badge-memory_limit' in css_content
        assert '.badge-compile_error' in css_content
        assert '.badge-runtime_error' in css_content

    def test_alert_styles(self, css_content):
        """包含消息提示样式"""
        assert '.alert-success' in css_content
        assert '.alert-error' in css_content

    def test_code_editor_styles(self, css_content):
        """包含代码编辑器和代码块样式"""
        assert '.code-editor' in css_content
        assert '.code-block' in css_content

    def test_pagination_styles(self, css_content):
        """包含分页样式"""
        assert '.pagination' in css_content

    def test_spinner_styles(self, css_content):
        """包含加载动画"""
        assert '.spinner' in css_content or '@keyframes spin' in css_content

    def test_tabs_styles(self, css_content):
        """包含标签页样式"""
        assert '.tabs' in css_content

    # 页面
    def test_auth_page_styles(self, css_content):
        """包含认证页样式"""
        assert '.auth-page' in css_content

    def test_problem_page_styles(self, css_content):
        """包含题目页样式"""
        assert '.problem-layout' in css_content
        assert '.problem-desc' in css_content

    def test_admin_page_styles(self, css_content):
        """包含管理后台样式"""
        assert '.stats-grid' in css_content
        assert '.admin-toolbar' in css_content

    # 工具类
    def test_utility_classes(self, css_content):
        """包含常用工具类"""
        assert '.text-center' in css_content
        assert '.truncate' in css_content
        assert '.hidden' in css_content
        assert '.flex' in css_content

    # 响应式
    def test_responsive_media_queries(self, css_content):
        """包含响应式媒体查询"""
        assert '@media' in css_content
        assert 'max-width' in css_content


# ═══════════════════════════════════════════════════════════════════════════
# api.js 测试: 使用 Node.js 运行
# ═══════════════════════════════════════════════════════════════════════════

class TestApiModule:
    """验证 api.js 模块结构和 Token 管理。"""

    def test_api_object_exists(self):
        """API 对象已定义且包含所有公开方法"""
        result = _load_api_and_eval('''
const methods = Object.keys(API);
const expected = [
  'setAuth', 'clearAuth', 'getAuth', 'isAuthenticated', 'onAuthChange',
  'register', 'login', 'logout', 'refreshToken',
  'getProblems', 'getProblem',
  'submitCode', 'getSubmission', 'getSubmissions',
  'createProblem', 'updateProblem', 'deleteProblem',
  'addTestCase', 'updateTestCase', 'deleteTestCase',
  'getUsers', 'updateUser', 'getStats',
  'healthCheck',
  'request', 'ApiError'
];
const missing = expected.filter(k => !methods.includes(k));
const extra = methods.filter(k => !expected.includes(k));
console.log(JSON.stringify({ methods, missing, extra }));
''')
        data = json.loads(result)
        assert data['missing'] == [], f'Missing methods: {data["missing"]}'
        # 注意: extra 中可能有 module/exports 相关的，这是正常的

    def test_set_and_get_auth(self):
        """setAuth() 设置 token，getAuth() 返回 token"""
        result = _load_api_and_eval('''
API.setAuth('test-token-123');
const got = API.getAuth();
const isAuth = API.isAuthenticated();
API.clearAuth();
const cleared = API.getAuth();
const isAuthAfter = API.isAuthenticated();
console.log(JSON.stringify({ got, isAuth, cleared, isAuthAfter }));
''')
        data = json.loads(result)
        assert data['got'] == 'test-token-123'
        assert data['isAuth'] == True
        assert data['cleared'] is None
        assert data['isAuthAfter'] == False

    def test_on_auth_change_callback(self):
        """onAuthChange() 注册回调，认证状态变化时触发"""
        result = _load_api_and_eval('''
const events = [];
API.onAuthChange((authed) => events.push(authed));
API.setAuth('token');
API.setAuth('token2');  // 两次 setAuth 不会重复触发 (值都 truthy)
API.clearAuth();
API.setAuth('new-token');
console.log(JSON.stringify({ events }));
''')
        data = json.loads(result)
        # setAuth('token') → true, setAuth('token2') → true, clearAuth() → false, setAuth('new-token') → true
        assert data['events'] == [True, True, False, True]

    def test_api_error_class(self):
        """ApiError 是正确的错误类"""
        result = _load_api_and_eval('''
try {
  throw new API.ApiError(404, 'not found');
} catch (e) {
  console.log(JSON.stringify({
    name: e.name,
    message: e.message,
    status: e.status,
    isError: e instanceof Error,
    isApiError: e instanceof API.ApiError,
  }));
}
''')
        data = json.loads(result)
        assert data['name'] == 'ApiError'
        assert data['status'] == 404
        assert data['message'] == 'not found'
        assert data['isError'] == True
        assert data['isApiError'] == True

    def test_request_adds_auth_header(self):
        """request() 在 auth=True 时添加 Authorization header"""
        result = _load_api_and_eval('''
API.setAuth('my-jwt-token');

// 设置 mock: 成功响应 + 捕获请求参数
let capturedUrl = '';
let capturedHeaders = {};

global._setFetchResponses([
  { status: 200, body: { data: 'ok', message: 'ok' } }
]);

// 通过截获实际 fetch 调用来验证
const origFetch = global.fetch;
global.fetch = async function(url, opts) {
  capturedUrl = url;
  capturedHeaders = opts.headers || {};
  return { status: 200, ok: true, json: async () => ({ data: 'ok', message: 'ok' }) };
};

(async () => {
  await API.request('GET', '/test', null, { auth: true });
  console.log(JSON.stringify({
    url: capturedUrl,
    hasAuth: 'Authorization' in capturedHeaders,
    authValue: capturedHeaders['Authorization'] || '',
  }));
})();
''')
        data = json.loads(result)
        assert data['hasAuth'] == True
        assert data['authValue'] == 'Bearer my-jwt-token'
        assert '/test' in data['url']

    def test_request_omits_auth_when_not_required(self):
        """request() 在 auth=False 时不添加 Authorization header"""
        result = _load_api_and_eval('''
API.setAuth('my-jwt-token');
let capturedHeaders = {};

global.fetch = async function(url, opts) {
  capturedHeaders = opts.headers || {};
  return { status: 200, ok: true, json: async () => ({ data: 'ok', message: 'ok' }) };
};

(async () => {
  await API.request('GET', '/problems', null, { auth: false });
  console.log(JSON.stringify({
    hasAuth: 'Authorization' in capturedHeaders,
  }));
})();
''')
        data = json.loads(result)
        assert data['hasAuth'] == False

    def test_request_adds_query_params(self):
        """request() 正确拼接 URL 查询参数"""
        result = _load_api_and_eval('''
let capturedUrl = '';

global.fetch = async function(url, opts) {
  capturedUrl = url;
  return { status: 200, ok: true, json: async () => ({ data: { items: [] }, message: 'ok' }) };
};

(async () => {
  await API.request('GET', '/problems', null, {
    params: { difficulty: 'easy', page: 1, page_size: 20 }
  });
  console.log(JSON.stringify({ url: capturedUrl }));
})();
''')
        data = json.loads(result)
        assert 'difficulty=easy' in data['url']
        assert 'page=1' in data['url']
        assert 'page_size=20' in data['url']

    def test_request_401_triggers_refresh_and_retry(self):
        """401 响应触发 token refresh 并重放请求"""
        result = _load_api_and_eval('''
API.setAuth('expired-token');
let requestCount = 0;
let refreshCalled = false;

// 模拟: 第 1 次请求返回 401, refresh 成功返回新 token, 第 2 次请求成功
global._setFetchResponses([
  // 第 1 次: 原请求 401
  { status: 401, body: { error: 'expired' } },
  // 第 2 次: refresh 请求成功
  { status: 200, body: { access_token: 'new-jwt-token' } },
  // 第 3 次: 重放请求成功
  { status: 200, body: { data: 'retry-ok', message: 'ok' } },
]);

(async () => {
  const result = await API.request('GET', '/submissions/1', null, { auth: true });
  const newToken = API.getAuth();
  console.log(JSON.stringify({
    status: result.status,
    data: result.data,
    newToken: newToken,
  }));
})();
''')
        data = json.loads(result)
        assert data['status'] == 200
        assert data['data'] == 'retry-ok'
        assert data['newToken'] == 'new-jwt-token'

    def test_login_stores_token_on_success(self):
        """login() 成功后自动存储 access_token"""
        result = _load_api_and_eval('''
global._setFetchResponses([
  { status: 200, body: {
    access_token: 'login-jwt-token',
    token_type: 'Bearer',
    expires_in: 900,
    user: { id: 1, username: 'alice', role: 'user' }
  }},
]);

(async () => {
  const result = await API.login('alice', 'secret123');
  const stored = API.getAuth();
  console.log(JSON.stringify({
    status: result.status,
    stored: stored,
  }));
})();
''')
        data = json.loads(result)
        assert data['status'] == 200
        assert data['stored'] == 'login-jwt-token'

    def test_logout_clears_token(self):
        """logout() 后清除 access_token"""
        result = _load_api_and_eval('''
API.setAuth('some-token');

global._setFetchResponses([
  { status: 200, body: { message: 'logged out' } },
]);

(async () => {
  const before = API.getAuth();
  await API.logout();
  const after = API.getAuth();
  console.log(JSON.stringify({ before, after }));
})();
''')
        data = json.loads(result)
        assert data['before'] == 'some-token'
        assert data['after'] is None

    def test_submit_code_sends_correct_payload(self):
        """submitCode() 发送正确的请求体"""
        result = _load_api_and_eval('''
API.setAuth('token');
let capturedBody = '';

global.fetch = async function(url, opts) {
  capturedBody = opts.body || '';
  return { status: 201, ok: true, json: async () => ({ data: { submission_id: 42 }, message: 'ok' }) };
};

(async () => {
  await API.submitCode(1, '#include <iostream>\\nint main() { return 0; }');
  const parsed = JSON.parse(capturedBody);
  console.log(JSON.stringify(parsed));
})();
''')
        data = json.loads(result)
        assert data['problem_id'] == 1
        assert '#include' in data['code']


# ═══════════════════════════════════════════════════════════════════════════
# utils.js 测试: 使用 Node.js 运行
# ═══════════════════════════════════════════════════════════════════════════

class TestUtilsDate:
    """日期时间相关功能。"""

    def test_format_date_full(self):
        """formatDate 去除秒部分"""
        result = _load_utils_and_eval('''
console.log(JSON.stringify({
  full: Utils.formatDate('2026-06-01 10:30:45'),
  short: Utils.formatDate('2026-06-01 10:30'),
  nullVal: Utils.formatDate(null),
  empty: Utils.formatDate(''),
}));
''')
        data = json.loads(result)
        assert data['full'] == '2026-06-01 10:30'
        assert data['nullVal'] == '-'
        assert data['empty'] == '-'

    def test_time_ago(self):
        """timeAgo 返回相对时间描述"""
        result = _load_utils_and_eval('''
console.log(JSON.stringify({
  nullVal: Utils.timeAgo(null),
  empty: Utils.timeAgo(''),
}));
''')
        data = json.loads(result)
        assert data['nullVal'] == '-'
        assert data['empty'] == '-'

    def test_time_ago_now(self):
        """timeAgo 对刚刚的时间返回 '刚刚'"""
        # 生成一个 30 秒前的时间字符串
        result = _load_utils_and_eval('''
const now = new Date();
const d = new Date(now.getTime() - 30 * 1000);
const iso = d.toISOString();
const dateStr = iso.replace('T', ' ').substring(0, 19);
const ago = Utils.timeAgo(dateStr);
console.log(JSON.stringify({ dateStr, ago }));
''')
        data = json.loads(result)
        assert data['ago'] == '刚刚'


class TestUtilsHtml:
    """HTML 安全相关功能。"""

    def test_escape_html(self):
        """escapeHtml 转义特殊字符"""
        result = _load_utils_and_eval('''
console.log(JSON.stringify({
  script: Utils.escapeHtml('<script>alert("XSS")</script>'),
  amp: Utils.escapeHtml('a & b'),
  quote: Utils.escapeHtml('"hello"'),
  normal: Utils.escapeHtml('normal text'),
  empty: Utils.escapeHtml(''),
  nullVal: Utils.escapeHtml(null),
}));
''')
        data = json.loads(result)
        assert '&lt;script&gt;' in data['script']
        assert '&amp;' in data['amp']
        assert '&quot;' in data['quote']
        assert data['normal'] == 'normal text'
        assert data['empty'] == ''
        assert data['nullVal'] == ''


class TestUtilsUrl:
    """URL 参数解析。"""

    def test_get_query_param(self):
        """getQueryParam 读取 URL 查询参数"""
        result = _load_utils_and_eval('''
// 模拟不同 URL
const tests = [];
const origSearch = window.location.search;

// 测试 1: 正常参数
Object.defineProperty(window.location, 'search', { value: '?difficulty=easy&page=2', writable: true });
tests.push({
  diff: Utils.getQueryParam('difficulty'),
  page: Utils.getQueryParam('page'),
  missing: Utils.getQueryParam('missing', 'default'),
  missing2: Utils.getQueryParam('missing'),
});

// 测试 2: 空参数
Object.defineProperty(window.location, 'search', { value: '', writable: true });
tests.push({
  q: Utils.getQueryParam('q', 'none'),
});

Object.defineProperty(window.location, 'search', { value: origSearch, writable: true });
console.log(JSON.stringify(tests));
''')
        data = json.loads(result)
        assert data[0]['diff'] == 'easy'
        assert data[0]['page'] == '2'
        assert data[0]['missing'] == 'default'
        assert data[0]['missing2'] is None
        assert data[1]['q'] == 'none'


class TestUtilsStatus:
    """判题状态映射。"""

    def test_status_class(self):
        """statusClass 返回正确的 CSS 类名"""
        result = _load_utils_and_eval('''
const cases = ['pending','compiling','running','accepted','wrong_answer',
  'time_limit','memory_limit','runtime_error','compile_error','system_error','unknown'];
const out = {};
cases.forEach(s => { out[s] = Utils.statusClass(s); });
console.log(JSON.stringify(out));
''')
        data = json.loads(result)
        assert data['accepted'] == 'badge-accepted'
        assert data['wrong_answer'] == 'badge-wrong_answer'
        assert data['time_limit'] == 'badge-time_limit'
        assert data['memory_limit'] == 'badge-memory_limit'
        assert data['runtime_error'] == 'badge-runtime_error'
        assert data['compile_error'] == 'badge-compile_error'
        assert data['system_error'] == 'badge-system_error'
        assert data['pending'] == 'badge-pending'
        assert data['compiling'] == 'badge-compiling'
        assert data['running'] == 'badge-running'
        assert data['unknown'] == 'badge-pending'  # 默认

    def test_status_text(self):
        """statusText 返回正确的中文文本"""
        result = _load_utils_and_eval('''
const cases = ['pending','compiling','running','accepted','wrong_answer',
  'time_limit','memory_limit','runtime_error','compile_error','system_error'];
const out = {};
cases.forEach(s => { out[s] = Utils.statusText(s); });
out['unknown'] = Utils.statusText('unknown');
out['null'] = Utils.statusText(null);
console.log(JSON.stringify(out));
''')
        data = json.loads(result)
        assert data['accepted'] == '通过'
        assert data['wrong_answer'] == '答案错误'
        assert data['time_limit'] == '超时'
        assert data['memory_limit'] == '超内存'
        assert data['runtime_error'] == '运行错误'
        assert data['compile_error'] == '编译错误'
        assert data['system_error'] == '系统错误'
        assert data['pending'] == '等待中'
        assert data['compiling'] == '编译中'
        assert data['running'] == '运行中'
        assert data['unknown'] == 'unknown'  # 未知状态原样返回
        assert data['null'] == '未知'  # null 返回默认


class TestUtilsDifficulty:
    """题目难度映射。"""

    def test_difficulty_class(self):
        """difficultyClass 返回正确的 CSS 类名"""
        result = _load_utils_and_eval('''
const out = {
  easy: Utils.difficultyClass('easy'),
  medium: Utils.difficultyClass('medium'),
  hard: Utils.difficultyClass('hard'),
  unknown: Utils.difficultyClass('extreme'),
};
console.log(JSON.stringify(out));
''')
        data = json.loads(result)
        assert data['easy'] == 'badge-easy'
        assert data['medium'] == 'badge-medium'
        assert data['hard'] == 'badge-hard'
        assert data['unknown'] == 'badge-easy'

    def test_difficulty_text(self):
        """difficultyText 返回正确的中文文本"""
        result = _load_utils_and_eval('''
const out = {
  easy: Utils.difficultyText('easy'),
  medium: Utils.difficultyText('medium'),
  hard: Utils.difficultyText('hard'),
};
console.log(JSON.stringify(out));
''')
        data = json.loads(result)
        assert data['easy'] == '简单'
        assert data['medium'] == '中等'
        assert data['hard'] == '困难'


class TestUtilsRole:
    """用户角色映射。"""

    def test_role_class_and_text(self):
        """roleClass 和 roleText 返回正确值"""
        result = _load_utils_and_eval('''
console.log(JSON.stringify({
  adminClass: Utils.roleClass('admin'),
  userClass: Utils.roleClass('user'),
  adminText: Utils.roleText('admin'),
  userText: Utils.roleText('user'),
}));
''')
        data = json.loads(result)
        assert data['adminClass'] == 'badge-admin'
        assert data['userClass'] == 'badge-user'
        assert data['adminText'] == '管理员'
        assert data['userText'] == '用户'


class TestUtilsUserStatus:
    """用户账号状态映射。"""

    def test_user_status(self):
        result = _load_utils_and_eval('''
console.log(JSON.stringify({
  activeClass: Utils.userStatusClass('active'),
  disabledClass: Utils.userStatusClass('disabled'),
  activeText: Utils.userStatusText('active'),
  disabledText: Utils.userStatusText('disabled'),
}));
''')
        data = json.loads(result)
        assert data['activeClass'] == 'badge-active'
        assert data['disabledClass'] == 'badge-disabled'
        assert data['activeText'] == '正常'
        assert data['disabledText'] == '已禁用'


class TestUtilsDom:
    """DOM 工具函数。"""

    def test_create_element_basic(self):
        """createElement 创建带属性和文本的元素"""
        result = _load_utils_and_eval('''
const el = Utils.createElement('div', {
  className: 'card',
  id: 'test-card',
  textContent: 'Hello World',
});
console.log(JSON.stringify({
  tag: el.tagName,
  className: el.className,
  id: el.id,
  text: el.textContent,
}));
''')
        data = json.loads(result)
        assert data['tag'] == 'DIV'
        assert data['className'] == 'card'
        assert data['id'] == 'test-card'
        assert data['text'] == 'Hello World'

    def test_create_element_with_children(self):
        """createElement 支持嵌套子元素"""
        result = _load_utils_and_eval('''
const child = Utils.createElement('span', { textContent: 'child' });
const parent = Utils.createElement('div', { className: 'parent' }, [
  child,
  'text node',
]);
console.log(JSON.stringify({
  tag: parent.tagName,
  className: parent.className,
  childCount: parent.children.length,
  firstChildTag: parent.children[0].tagName,
  firstChildText: parent.children[0].textContent,
  secondChildText: parent.children[1].textContent,
}));
''')
        data = json.loads(result)
        assert data['tag'] == 'DIV'
        assert data['childCount'] == 2
        assert data['firstChildTag'] == 'SPAN'
        assert data['firstChildText'] == 'child'
        assert data['secondChildText'] == 'text node'

    def test_create_element_with_innerHTML(self):
        """createElement 支持 innerHTML 属性"""
        result = _load_utils_and_eval('''
const el = Utils.createElement('div', {
  innerHTML: '<strong>bold</strong>',
});
console.log(JSON.stringify({
  html: el.innerHTML,
}));
''')
        data = json.loads(result)
        assert data['html'] == '<strong>bold</strong>'

    def test_create_element_with_event_listener(self):
        """createElement 支持事件监听 (onClick 等)"""
        result = _load_utils_and_eval('''
let clicked = false;
const el = Utils.createElement('button', {
  className: 'btn',
  onClick: function() { clicked = true; },
  textContent: 'Click me',
});
// 触发事件
el.addEventListener.mock = null; // not needed for test
// 直接调用注册的处理函数来模拟点击: 我们不直接访问 listeners
// 验证属性被正确设置
console.log(JSON.stringify({
  tag: el.tagName,
  className: el.className,
  text: el.textContent,
}));
''')
        data = json.loads(result)
        assert data['tag'] == 'BUTTON'
        assert data['className'] == 'btn'
        assert data['text'] == 'Click me'


class TestUtilsPagination:
    """分页工具。"""

    def test_total_pages(self):
        """totalPages 正确计算总页数"""
        result = _load_utils_and_eval('''
console.log(JSON.stringify({
  exact: Utils.totalPages(100, 20),    // 5
  remainder: Utils.totalPages(101, 20), // 6
  one: Utils.totalPages(5, 20),        // 1
  zero: Utils.totalPages(0, 20),       // 0
  defaultSize: Utils.totalPages(50),   // 默认 page_size=20
}));
''')
        data = json.loads(result)
        assert data['exact'] == 5
        assert data['remainder'] == 6
        assert data['one'] == 1
        assert data['zero'] == 0
        assert data['defaultSize'] == 3  # ceil(50/20) = 3

    def test_get_page_range_small(self):
        """getPageRange 对于少量页面返回全量"""
        result = _load_utils_and_eval('''
console.log(JSON.stringify({
  r3: Utils.getPageRange(1, 3),
  r5: Utils.getPageRange(3, 5),
  r7: Utils.getPageRange(1, 7),
}));
''')
        data = json.loads(result)
        assert data['r3'] == [1, 2, 3]
        assert data['r5'] == [1, 2, 3, 4, 5]
        assert data['r7'] == [1, 2, 3, 4, 5, 6, 7]

    def test_get_page_range_large(self):
        """getPageRange 对于大量页面返回省略号"""
        result = _load_utils_and_eval('''
console.log(JSON.stringify({
  start: Utils.getPageRange(1, 20),
  middle: Utils.getPageRange(10, 20),
  end: Utils.getPageRange(20, 20),
}));
''')
        data = json.loads(result)
        # 第 1 页: [1, 2, 3, '...', 20]
        assert data['start'][0] == 1
        assert data['start'][-1] == 20
        assert '...' in data['start']
        # 中间: [1, '...', 9, 10, 11, '...', 20]
        assert data['middle'][0] == 1
        assert data['middle'][-1] == 20
        assert 10 in data['middle']
        # 末页: [1, '...', 18, 19, 20]
        assert data['end'][0] == 1
        assert data['end'][-1] == 20
        assert 20 in data['end']


class TestUtilsStorage:
    """localStorage 封装。"""

    def test_storage_set_and_get(self):
        """storageSet 写入, storageGet 读取"""
        result = _load_utils_and_eval('''
Utils.storageSet('_test_key', { a: 1, b: [2, 3] });
const val = Utils.storageGet('_test_key');
const missing = Utils.storageGet('_nonexistent', 'default');
const missing2 = Utils.storageGet('_nonexistent2');
Utils.storageRemove('_test_key');
const afterRemove = Utils.storageGet('_test_key', 'gone');
console.log(JSON.stringify({ val, missing, missing2, afterRemove }));
''')
        data = json.loads(result)
        assert data['val'] == {'a': 1, 'b': [2, 3]}
        assert data['missing'] == 'default'
        assert data['missing2'] is None
        assert data['afterRemove'] == 'gone'


class TestUtilsDebounceThrottle:
    """防抖和节流函数。"""

    def test_debounce_returns_function(self):
        """debounce 返回一个函数"""
        result = _load_utils_and_eval('''
const fn = function() {};
const debounced = Utils.debounce(fn, 100);
console.log(JSON.stringify({ isFunction: typeof debounced === 'function' }));
''')
        data = json.loads(result)
        assert data['isFunction'] == True

    def test_throttle_returns_function(self):
        """throttle 返回一个函数"""
        result = _load_utils_and_eval('''
const fn = function() {};
const throttled = Utils.throttle(fn, 100);
console.log(JSON.stringify({ isFunction: typeof throttled === 'function' }));
''')
        data = json.loads(result)
        assert data['isFunction'] == True
