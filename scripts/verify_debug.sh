#!/usr/bin/env bash
# verify_debug.sh — 单文件调试模式：输出 AST / IR / ASM 中间产物，并与 Clang 对比验证
#
# 用法:  bash scripts/verify_debug.sh <源目录> <文件名>
# 示例:  bash scripts/verify_debug.sh examples/compiler_inputs 01_minimal.c

set -eu
[[ -n "${BASH_VERSION:-}" ]] && set -o pipefail

# ========== 参数检查 ==========
if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <source_dir> <file_name>"
  echo "Example: $0 examples/compiler_inputs 01_minimal.c"
  exit 1
fi

SRC_DIR="$1"
FILE_NAME="$2"

# 智能处理：如包含路径分隔符则视为相对路径
if [[ "$FILE_NAME" == *"/"* ]] || [[ "$FILE_NAME" == *"\\"* ]]; then
  c_file="$FILE_NAME"
else
  c_file="$SRC_DIR/$FILE_NAME"
fi

if [[ ! -f "$c_file" ]]; then
  echo "Error: Source file '$c_file' does not exist"
  exit 1
fi

# ========== 查找 ToyC 可执行文件 ==========
TOYC=""
for candidate in ./build-linux/toyc ./build/toyc ./build/toyc.exe ./toyc ./toyc.exe; do
  [[ -f "$candidate" ]] && { TOYC="$candidate"; break; }
done
if [[ -z "$TOYC" ]]; then
  echo "Error: ToyC compiler not found (searched ./build/ and ./)"
  echo "Please run 'cmake --build build' first."
  exit 1
fi

base="$(basename "$c_file" .c)"
DEBUG_DIR="examples/debug"
ASM_DIR="examples/asm"
IR_DIR="examples/ir"

# 清理旧输出并创建目录
rm -f "$DEBUG_DIR/${base}"_* 2>/dev/null || true
mkdir -p "$DEBUG_DIR" "$ASM_DIR" "$IR_DIR"

echo "========================================="
echo "  ToyC Compiler Debug Mode"
echo "========================================="
echo "Source:  $c_file"
echo "ToyC:   $TOYC"
echo "Output: $DEBUG_DIR/"
echo ""

# ---------- Step 1: 生成 AST ----------
AST_OUT="$DEBUG_DIR/${base}_toyc_ast.txt"
echo "─────────────────────────────────────────"
echo "Step 1: Generating AST ..."
if "$TOYC" "$c_file" --ast > "$AST_OUT" 2>/dev/null; then
  echo "✅ AST → $AST_OUT"
  echo ""
  cat "$AST_OUT"
  echo ""
else
  echo "❌ AST generation failed"
  exit 1
fi

# ---------- Step 2: 生成 LLVM IR ----------
IR_OUT="$DEBUG_DIR/${base}_toyc_ir.ll"
echo "─────────────────────────────────────────"
echo "Step 2: Generating LLVM IR ..."
if "$TOYC" "$c_file" --ir 2>/dev/null | tee /dev/stderr | grep -v '^=== ' > "$IR_OUT" 2>/dev/null; then
  if [[ -s "$IR_OUT" ]]; then
    echo ""
    echo "✅ IR → $IR_OUT"
  else
    echo "❌ IR generation failed (empty output)"
    exit 1
  fi
else
  echo "❌ IR generation failed"
  exit 1
fi
echo ""

# ---------- Step 3: 生成 RISC-V 汇编（直接从源码） ----------
ASM_OUT="$DEBUG_DIR/${base}_toyc_asm.s"
echo "─────────────────────────────────────────"
echo "Step 3: Generating RISC-V Assembly ..."
if "$TOYC" "$c_file" -o "$ASM_OUT" 2>/dev/null; then
  echo "✅ ASM → $ASM_OUT"
  echo ""
  cat "$ASM_OUT"
  echo ""
else
  echo "❌ Assembly generation failed"
  exit 1
fi

# ---------- Step 4: Clang 参考输出 ----------
CLANG_IR_OUT="$DEBUG_DIR/${base}_clang_ir.ll"
CLANG_ASM_OUT="$DEBUG_DIR/${base}_clang_asm.s"
echo "─────────────────────────────────────────"
echo "Step 4: Generating Clang reference outputs ..."
echo ""

if command -v clang >/dev/null 2>&1; then
  # Clang IR
  if clang -S -emit-llvm -O0 "$c_file" -o "$CLANG_IR_OUT" 2>/dev/null; then
    echo "✅ Clang IR  → $CLANG_IR_OUT"
  else
    echo "⚠  Clang IR  → FAILED"
  fi
  # Clang ASM
  if clang -S --target=riscv32-unknown-elf -march=rv32im -mabi=ilp32 \
       "$c_file" -o "$CLANG_ASM_OUT" 2>/dev/null; then
    echo "✅ Clang ASM → $CLANG_ASM_OUT"
  else
    echo "⚠  Clang ASM → FAILED"
  fi
else
  echo "⚠  Clang not found — skipping reference outputs"
fi

# ---------- Step 5: 复制到测试目录 ----------
echo ""
echo "─────────────────────────────────────────"
echo "Step 5: Preparing test directories ..."

[[ -f "$ASM_OUT" && -s "$ASM_OUT" ]] && cp "$ASM_OUT" "$ASM_DIR/${base}_toyc.s"   && echo "  ✅ $ASM_DIR/${base}_toyc.s"
[[ -f "$IR_OUT"  && -s "$IR_OUT"  ]] && cp "$IR_OUT"  "$IR_DIR/${base}_toyc.ll"   && echo "  ✅ $IR_DIR/${base}_toyc.ll"
[[ -f "$CLANG_ASM_OUT" && -s "$CLANG_ASM_OUT" ]] && cp "$CLANG_ASM_OUT" "$ASM_DIR/${base}_clang.s" && echo "  ✅ $ASM_DIR/${base}_clang.s"
[[ -f "$CLANG_IR_OUT"  && -s "$CLANG_IR_OUT"  ]] && cp "$CLANG_IR_OUT"  "$IR_DIR/${base}_clang.ll" && echo "  ✅ $IR_DIR/${base}_clang.ll"

# ---------- Step 6: 运行验证 ----------
echo ""
echo "─────────────────────────────────────────"
echo "Step 6: Running verification ..."
echo ""

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERIFY_SCRIPT="$SCRIPT_DIR/verify_output.sh"

if [[ -f "$VERIFY_SCRIPT" ]]; then
  bash "$VERIFY_SCRIPT" "$SRC_DIR" "$base.c"
else
  echo "⚠  Verification script not found: $VERIFY_SCRIPT"
  echo "Debug outputs generated successfully."
fi
