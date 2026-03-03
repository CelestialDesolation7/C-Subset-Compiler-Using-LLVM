#!/usr/bin/env bash
# generate_ast.sh — 批量生成 ToyC 的 AST 输出，用于分析和对比
#
# 用法:  bash scripts/generate_ast.sh [源目录]
# 默认:  examples/compiler_inputs

set -eu
[[ -n "${BASH_VERSION:-}" ]] && set -o pipefail

SRC_DIR="${1:-examples/compiler_inputs}"
OUT_DIR="examples/ast"

# ---------- 查找 ToyC 可执行文件 ----------
TOYC=""
for candidate in ./build-linux/toyc ./build/toyc ./build/toyc.exe ./toyc ./toyc.exe; do
  [[ -f "$candidate" ]] && { TOYC="$candidate"; break; }
done
if [[ -z "$TOYC" ]]; then
  echo "Error: ToyC compiler not found (searched ./build/ and ./)"
  echo "Please run 'cmake --build build' first."
  exit 1
fi
echo "Using ToyC: $TOYC"

# ---------- 检查源目录 ----------
if [[ ! -d "$SRC_DIR" ]]; then
  echo "Error: Source directory '$SRC_DIR' does not exist"
  exit 1
fi

mkdir -p "$OUT_DIR"

TOTAL=0; OK=0; FAIL=0

for c in "$SRC_DIR"/*.c; do
  [[ ! -f "$c" ]] && { echo "No .c files found in $SRC_DIR"; exit 0; }

  base="$(basename "$c" .c)"
  TOTAL=$((TOTAL + 1))
  echo ">>> $base"

  # ToyC → AST（--ast 输出到 stdout，去掉 "=== AST ===" 标题行）
  if "$TOYC" "$c" --ast 2>/dev/null | grep -v '^=== ' > "$OUT_DIR/${base}_toyc.ast"; then
    if [[ -s "$OUT_DIR/${base}_toyc.ast" ]]; then
      echo "  ToyC AST → $OUT_DIR/${base}_toyc.ast"
    else
      echo "  ToyC AST → FAILED (empty output)"
      rm -f "$OUT_DIR/${base}_toyc.ast"
      FAIL=$((FAIL + 1))
      continue
    fi
  else
    echo "  ToyC AST → FAILED"
    rm -f "$OUT_DIR/${base}_toyc.ast"
    FAIL=$((FAIL + 1))
    continue
  fi

  OK=$((OK + 1))
  echo
done

echo "========================================="
echo "Done: $OK/$TOTAL succeeded, $FAIL failed"
echo "Output directory: $OUT_DIR/"
