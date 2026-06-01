#!/usr/bin/env bash
# =============================================================================
# VibeOJ Python API Test Runner
# =============================================================================
# 用法:
#   ./run_tests.sh                  # 运行全部测试
#   ./run_tests.sh -m smoke         # 仅冒烟测试
#   ./run_tests.sh -m admin         # 仅 admin 模块
#   ./run_tests.sh --html=report.html  # 生成 HTML 报告
#   ./run_tests.sh -n 4             # 4 线程并行运行
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 检查 venv
if [ -f "../../.venv/bin/activate" ]; then
  source "../../.venv/bin/activate"
elif [ -f "../../venv/bin/activate" ]; then
  source "../../venv/bin/activate"
elif [ -f "../venv/bin/activate" ]; then
  source "../venv/bin/activate"
else
  echo "⚠ 未找到 venv，使用系统 Python"
fi

# 检查依赖
python -c "import pytest" 2>/dev/null || {
  echo "❌ pytest 未安装，请先安装依赖:"
  echo "   pip install -r requirements.txt"
  exit 1
}

# 默认参数
EXTRA_ARGS="${@:--v}"

# 运行测试
echo "═══════════════════════════════════════════════════════════════"
echo "  VibeOJ Python API Tests"
echo "  Base URL: ${API_BASE_URL:-http://localhost:8080/api/v1}"
echo "═══════════════════════════════════════════════════════════════"
echo ""

python -m pytest $EXTRA_ARGS

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  Tests complete."
echo "═══════════════════════════════════════════════════════════════"
