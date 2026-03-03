#!/usr/bin/env bash
# generate_ir.sh — 批量生成 ToyC 与 Clang 的 LLVM IR（.ll），用于对比
#
# 用法:  bash scripts/generate_ir.sh [源目录]
# 默认:  examples/compiler_inputs

set -eu
[[ -n "${BASH_VERSION:-}" ]] && set -o pipefail

SRC_DIR="${1:-examples/compiler_inputs}"
OUT_DIR="examples/ir"

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
  echo "─── $base ───"

  # 1. ToyC → LLVM IR（--ir 输出到 stdout，去掉 "=== LLVM IR ===" 标题行）
  if "$TOYC" "$c" --ir 2>/dev/null | grep -v '^=== ' > "$OUT_DIR/${base}_toyc.ll"; then
    if [[ -s "$OUT_DIR/${base}_toyc.ll" ]]; then
      echo "  ToyC  IR → $OUT_DIR/${base}_toyc.ll"
    else
      echo "  ToyC  IR → FAILED (empty output)"
      rm -f "$OUT_DIR/${base}_toyc.ll"
      FAIL=$((FAIL + 1))
      continue
    fi
  else
    echo "  ToyC  IR → FAILED"
    rm -f "$OUT_DIR/${base}_toyc.ll"
    FAIL=$((FAIL + 1))
    continue
  fi

  # 2. Clang → LLVM IR
  if command -v clang >/dev/null 2>&1; then
    if clang -S -emit-llvm -O0 "$c" -o "$OUT_DIR/${base}_clang.ll" 2>/dev/null; then
      echo "  Clang IR → $OUT_DIR/${base}_clang.ll"
    else
      echo "  Clang IR → FAILED"
    fi
  else
    echo "  Clang IR → skipped (not installed)"
  fi

  OK=$((OK + 1))
  echo
done

echo "========================================="
echo "Done: $OK/$TOTAL succeeded, $FAIL failed"
echo "Output directory: $OUT_DIR/"
