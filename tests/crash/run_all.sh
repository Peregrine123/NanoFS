#!/bin/bash
# 运行所有崩溃测试

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "╔════════════════════════════════════════╗"
echo "║  ModernFS 崩溃测试套件                 ║"
echo "║  验证WAL日志的崩溃一致性               ║"
echo "╚════════════════════════════════════════╝"
echo ""

TESTS=(
    "crash_during_write.sh"
    "crash_after_commit.sh"
)

PASSED=0
FAILED=0

for test in "${TESTS[@]}"; do
    if [ -f "$test" ]; then
        echo "────────────────────────────────────────"
        echo "运行: $test"
        echo "────────────────────────────────────────"

        if bash "$test"; then
            PASSED=$((PASSED + 1))
        else
            FAILED=$((FAILED + 1))
            echo "❌ $test 失败"
        fi
        echo ""
    else
        echo "⚠️  测试文件不存在: $test"
    fi
done

echo "════════════════════════════════════════"
echo "  崩溃测试汇总"
echo "════════════════════════════════════════"
echo "  通过: $PASSED"
echo "  失败: $FAILED"
echo "  总计: $((PASSED + FAILED))"
echo "════════════════════════════════════════"

if [ $FAILED -eq 0 ]; then
    echo ""
    echo "✅ 所有崩溃测试通过！"
    echo ""
    exit 0
else
    echo ""
    echo "❌ 有 $FAILED 个测试失败"
    echo ""
    exit 1
fi
