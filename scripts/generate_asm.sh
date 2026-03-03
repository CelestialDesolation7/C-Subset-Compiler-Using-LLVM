#!/usr/bin/env bash
# generate_asm.sh — 批量生成 ToyC 与 Clang 的 RISC-V 汇编，用于对比
#
# 用法:  bash scripts/generate_asm.sh [源目录]
# 默认:  examples/compiler_inputs

set -eu
[[ -n "${BASH_VERSION:-}" ]] && set -o pipefail

SRC_DIR="${1:-examples/compiler_inputs}"
OUT_DIR="examples/asm"

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

  # 1. ToyC → asm（使用 -o 将汇编写入文件）
  if "$TOYC" "$c" -o "$OUT_DIR/${base}_toyc.s" 2>/dev/null; then
    echo "  ToyC  → $OUT_DIR/${base}_toyc.s"
  else
    echo "  ToyC  → FAILED"
    FAIL=$((FAIL + 1))
    continue
  fi

  # 2. Clang → asm（需安装 clang + RISC-V 后端）
  if command -v clang >/dev/null 2>&1; then
    if clang -S --target=riscv32-unknown-elf -march=rv32im -mabi=ilp32 \
         -O0 "$c" -o "$OUT_DIR/${base}_clang.s" 2>/dev/null; then
      echo "  Clang → $OUT_DIR/${base}_clang.s"
    else
      echo "  Clang → FAILED"
    fi
  else
    echo "  Clang → skipped (not installed)"
  fi

  OK=$((OK + 1))
  echo
done

echo "========================================="
echo "Done: $OK/$TOTAL succeeded, $FAIL failed"
echo "Output directory: $OUT_DIR/"
