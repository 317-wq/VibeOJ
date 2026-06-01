/* ==========================================================================
 * VibeOJ — 通用工具模块
 * 提供日期格式化、HTML 转义、URL 解析、UI 辅助等工具函数。
 *
 * 用法：
 *   import { Utils } from './utils.js';
 *   Utils.formatDate('2026-06-01 10:00:00');  // → '2026-06-01 10:00'
 * ========================================================================== */

const Utils = (function () {
  'use strict';

  /* ═══════════════════════════════════════════════════════════════════════
   * 日期时间
   * ═══════════════════════════════════════════════════════════════════════ */

  /**
   * 格式化日期字符串为更友好的显示格式。
   * 输入格式: YYYY-MM-DD HH:MM:SS
   * 输出格式: YYYY-MM-DD HH:MM (去除秒)
   *
   * @param {string} dateStr - 日期字符串
   * @returns {string}
   */
  function formatDate(dateStr) {
    if (!dateStr) return '-';
    // 截取前 16 个字符: "YYYY-MM-DD HH:MM"
    return dateStr.substring(0, 16);
  }

  /**
   * 格式化日期为相对时间描述。
   * 例如: "刚刚", "5 分钟前", "2 小时前", "3 天前", "2026-06-01"
   *
   * @param {string} dateStr - 日期字符串 (YYYY-MM-DD HH:MM:SS)
   * @returns {string}
   */
  function timeAgo(dateStr) {
    if (!dateStr) return '-';
    const now = new Date();
    const date = new Date(dateStr.replace(' ', 'T') + 'Z');  // 视为 UTC
    if (isNaN(date.getTime())) return dateStr;

    const diff = Math.max(0, now - date);
    const seconds = Math.floor(diff / 1000);
    const minutes = Math.floor(seconds / 60);
    const hours = Math.floor(minutes / 60);
    const days = Math.floor(hours / 24);

    if (seconds < 60)  return '刚刚';
    if (minutes < 60)  return minutes + ' 分钟前';
    if (hours < 24)    return hours + ' 小时前';
    if (days < 30)     return days + ' 天前';
    return formatDate(dateStr);
  }

  /* ═══════════════════════════════════════════════════════════════════════
   * HTML 安全
   * ═══════════════════════════════════════════════════════════════════════ */

  /**
   * HTML 转义 — 防止 XSS。
   *
   * @param {string} str - 原始字符串
   * @returns {string} 转义后的安全字符串
   */
  function escapeHtml(str) {
    if (!str) return '';
    const entityMap = {
      '&': '&amp;',
      '<': '&lt;',
      '>': '&gt;',
      '"': '&quot;',
      "'": '&#39;',
    };
    return String(str).replace(/[&<>"']/g, function (ch) {
      return entityMap[ch];
    });
  }

  /* ═══════════════════════════════════════════════════════════════════════
   * URL 参数
   * ═══════════════════════════════════════════════════════════════════════ */

  /**
   * 从当前页面 URL 中读取指定查询参数。
   *
   * @param {string} name - 参数名
   * @param {string} [defaultVal] - 默认值
   * @returns {string|null}
   */
  function getQueryParam(name, defaultVal) {
    const params = new URLSearchParams(window.location.search);
    return params.get(name) || defaultVal || null;
  }

  /**
   * 解析当前页面所有查询参数。
   *
   * @returns {object}
   */
  function getQueryParams() {
    const params = new URLSearchParams(window.location.search);
    const result = {};
    for (const [key, value] of params.entries()) {
      result[key] = value;
    }
    return result;
  }

  /* ═══════════════════════════════════════════════════════════════════════
   * 函数工具
   * ═══════════════════════════════════════════════════════════════════════ */

  /**
   * 防抖 — 在连续调用时只执行最后一次。
   *
   * @param {Function} fn    - 需要防抖的函数
   * @param {number}   delay - 延迟毫秒数 (默认 300ms)
   * @returns {Function}
   */
  function debounce(fn, delay) {
    delay = delay || 300;
    var timer = null;
    return function () {
      var context = this;
      var args = arguments;
      if (timer) clearTimeout(timer);
      timer = setTimeout(function () {
        fn.apply(context, args);
      }, delay);
    };
  }

  /**
   * 节流 — 在指定间隔内只执行一次。
   *
   * @param {Function} fn       - 需要节流的函数
   * @param {number}   interval - 间隔毫秒数 (默认 300ms)
   * @returns {Function}
   */
  function throttle(fn, interval) {
    interval = interval || 300;
    var lastTime = 0;
    return function () {
      var now = Date.now();
      if (now - lastTime >= interval) {
        lastTime = now;
        fn.apply(this, arguments);
      }
    };
  }

  /* ═══════════════════════════════════════════════════════════════════════
   * 判题状态 → UI 映射
   * ═══════════════════════════════════════════════════════════════════════ */

  /** 判题状态 → CSS 类名映射 */
  var STATUS_CLASS_MAP = {
    pending:        'badge-pending',
    compiling:      'badge-compiling',
    running:        'badge-running',
    accepted:       'badge-accepted',
    wrong_answer:   'badge-wrong_answer',
    time_limit:     'badge-time_limit',
    memory_limit:   'badge-memory_limit',
    runtime_error:  'badge-runtime_error',
    compile_error:  'badge-compile_error',
    system_error:   'badge-system_error',
  };

  /** 判题状态 → 中文文本映射 */
  var STATUS_TEXT_MAP = {
    pending:        '等待中',
    compiling:      '编译中',
    running:        '运行中',
    accepted:       '通过',
    wrong_answer:   '答案错误',
    time_limit:     '超时',
    memory_limit:   '超内存',
    runtime_error:  '运行错误',
    compile_error:  '编译错误',
    system_error:   '系统错误',
  };

  /**
   * 获取判题状态的 CSS 类名。
   * @param {string} status
   * @returns {string}
   */
  function statusClass(status) {
    return STATUS_CLASS_MAP[status] || 'badge-pending';
  }

  /**
   * 获取判题状态的中文文本。
   * @param {string} status
   * @returns {string}
   */
  function statusText(status) {
    return STATUS_TEXT_MAP[status] || status || '未知';
  }

  /* ═══════════════════════════════════════════════════════════════════════
   * 题目难度 → UI 映射
   * ═══════════════════════════════════════════════════════════════════════ */

  /** 难度 → CSS 类名 */
  var DIFFICULTY_CLASS_MAP = {
    easy:   'badge-easy',
    medium: 'badge-medium',
    hard:   'badge-hard',
  };

  /** 难度 → 中文文本 */
  var DIFFICULTY_TEXT_MAP = {
    easy:   '简单',
    medium: '中等',
    hard:   '困难',
  };

  /**
   * 获取题目难度的 CSS 类名。
   * @param {string} difficulty
   * @returns {string}
   */
  function difficultyClass(difficulty) {
    return DIFFICULTY_CLASS_MAP[difficulty] || 'badge-easy';
  }

  /**
   * 获取题目难度的中文文本。
   * @param {string} difficulty
   * @returns {string}
   */
  function difficultyText(difficulty) {
    return DIFFICULTY_TEXT_MAP[difficulty] || difficulty || '未知';
  }

  /* ═══════════════════════════════════════════════════════════════════════
   * 用户角色 → UI 映射
   * ═══════════════════════════════════════════════════════════════════════ */

  /** 角色 → CSS 类名 */
  var ROLE_CLASS_MAP = {
    admin: 'badge-admin',
    user:  'badge-user',
  };

  /** 角色 → 中文文本 */
  var ROLE_TEXT_MAP = {
    admin: '管理员',
    user:  '用户',
  };

  /**
   * 获取角色的 CSS 类名。
   * @param {string} role
   * @returns {string}
   */
  function roleClass(role) {
    return ROLE_CLASS_MAP[role] || 'badge-user';
  }

  /**
   * 获取角色的中文文本。
   * @param {string} role
   * @returns {string}
   */
  function roleText(role) {
    return ROLE_TEXT_MAP[role] || role || '未知';
  }

  /* ═══════════════════════════════════════════════════════════════════════
   * 用户状态 → UI 映射
   * ═══════════════════════════════════════════════════════════════════════ */

  /**
   * 获取用户账号状态的 CSS 类名。
   * @param {string} status
   * @returns {string}
   */
  function userStatusClass(status) {
    return status === 'active' ? 'badge-active' : 'badge-disabled';
  }

  /**
   * 获取用户账号状态的中文文本。
   * @param {string} status
   * @returns {string}
   */
  function userStatusText(status) {
    return status === 'active' ? '正常' : '已禁用';
  }

  /* ═══════════════════════════════════════════════════════════════════════
   * DOM 工具
   * ═══════════════════════════════════════════════════════════════════════ */

  /**
   * 通过 ID 获取元素。若不存在返回 null 而非抛错。
   * @param {string} id
   * @returns {HTMLElement|null}
   */
  function $(id) {
    return document.getElementById(id);
  }

  /**
   * 通过选择器获取第一个元素。
   * @param {string} selector
   * @param {Element} [context]
   * @returns {HTMLElement|null}
   */
  function qs(selector, context) {
    return (context || document).querySelector(selector);
  }

  /**
   * 通过选择器获取所有匹配元素。
   * @param {string} selector
   * @param {Element} [context]
   * @returns {NodeList}
   */
  function qsa(selector, context) {
    return (context || document).querySelectorAll(selector);
  }

  /**
   * 创建元素并设置属性。
   *
   * @param {string} tag        - HTML 标签名
   * @param {object} [attrs]    - 属性对象 { className: '...', id: '...', ... }
   * @param {string|Node|Array} [children] - 子节点 (文本、元素或数组)
   * @returns {HTMLElement}
   */
  function createElement(tag, attrs, children) {
    var el = document.createElement(tag);
    if (attrs) {
      for (var key in attrs) {
        if (attrs.hasOwnProperty(key)) {
          if (key === 'className') {
            el.className = attrs[key];
          } else if (key === 'style' && typeof attrs[key] === 'object') {
            Object.assign(el.style, attrs[key]);
          } else if (key === 'dataset' && typeof attrs[key] === 'object') {
            Object.assign(el.dataset, attrs[key]);
          } else if (key === 'innerHTML') {
            el.innerHTML = attrs[key];
          } else if (key === 'textContent') {
            el.textContent = attrs[key];
          } else if (key.startsWith('on')) {
            el.addEventListener(key.slice(2).toLowerCase(), attrs[key]);
          } else {
            el.setAttribute(key, attrs[key]);
          }
        }
      }
    }
    if (children !== undefined && children !== null) {
      appendChildren(el, children);
    }
    return el;
  }

  /**
   * 向容器批量追加子节点。
   * @param {HTMLElement} container
   * @param {string|Node|Array} children
   */
  function appendChildren(container, children) {
    if (typeof children === 'string') {
      container.appendChild(document.createTextNode(children));
    } else if (children instanceof Node) {
      container.appendChild(children);
    } else if (Array.isArray(children)) {
      for (var i = 0; i < children.length; i++) {
        appendChildren(container, children[i]);
      }
    }
  }

  /* ═══════════════════════════════════════════════════════════════════════
   * 分页
   * ═══════════════════════════════════════════════════════════════════════ */

  /**
   * 计算总页数。
   * @param {number} total     - 总记录数
   * @param {number} pageSize  - 每页条数
   * @returns {number}
   */
  function totalPages(total, pageSize) {
    return Math.ceil(total / (pageSize || 20));
  }

  /**
   * 生成页码数组 (用于渲染分页按钮)。
   * 当总页数较多时，只显示首尾和当前页附近的页码。
   *
   * @param {number} current - 当前页码 (1-based)
   * @param {number} total   - 总页数
   * @returns {Array<number|string>} 包含页码数字和 '...' 字符串
   */
  function getPageRange(current, total) {
    if (total <= 7) {
      var range = [];
      for (var i = 1; i <= total; i++) range.push(i);
      return range;
    }

    var pages = [1];

    if (current > 3) pages.push('...');

    var start = Math.max(2, current - 1);
    var end = Math.min(total - 1, current + 1);

    for (var j = start; j <= end; j++) {
      pages.push(j);
    }

    if (current < total - 2) pages.push('...');

    pages.push(total);
    return pages;
  }

  /* ═══════════════════════════════════════════════════════════════════════
   * 存储 (localStorage 封装)
   * ═══════════════════════════════════════════════════════════════════════ */

  /**
   * 读取 localStorage 键值 (自动 JSON.parse)。
   * @param {string} key
   * @param {*} [defaultVal]
   * @returns {*}
   */
  function storageGet(key, defaultVal) {
    try {
      var val = localStorage.getItem(key);
      return val ? JSON.parse(val) : (defaultVal !== undefined ? defaultVal : null);
    } catch (_) {
      return defaultVal !== undefined ? defaultVal : null;
    }
  }

  /**
   * 写入 localStorage 键值 (自动 JSON.stringify)。
   * @param {string} key
   * @param {*} value
   */
  function storageSet(key, value) {
    try {
      localStorage.setItem(key, JSON.stringify(value));
    } catch (_) {
      // quota exceeded or disabled — 静默失败
    }
  }

  /**
   * 删除 localStorage 键值。
   * @param {string} key
   */
  function storageRemove(key) {
    try {
      localStorage.removeItem(key);
    } catch (_) {}
  }

  /* ═══════════════════════════════════════════════════════════════════════
   * 公开 API
   * ═══════════════════════════════════════════════════════════════════════ */

  return {
    // 日期
    formatDate:   formatDate,
    timeAgo:      timeAgo,

    // 安全
    escapeHtml:   escapeHtml,

    // URL
    getQueryParam:  getQueryParam,
    getQueryParams: getQueryParams,

    // 函数
    debounce:  debounce,
    throttle:  throttle,

    // 判题状态
    statusClass: statusClass,
    statusText:  statusText,

    // 题目难度
    difficultyClass: difficultyClass,
    difficultyText:  difficultyText,

    // 用户角色
    roleClass: roleClass,
    roleText:  roleText,

    // 用户状态
    userStatusClass: userStatusClass,
    userStatusText:  userStatusText,

    // DOM
    $:              $,
    qs:             qs,
    qsa:            qsa,
    createElement:  createElement,
    appendChildren: appendChildren,

    // 分页
    totalPages:   totalPages,
    getPageRange: getPageRange,

    // 存储
    storageGet:    storageGet,
    storageSet:    storageSet,
    storageRemove: storageRemove,
  };
})();

// 支持 ES module 和 <script> 引入
if (typeof module !== 'undefined' && module.exports) {
  module.exports = { Utils: Utils };
}
