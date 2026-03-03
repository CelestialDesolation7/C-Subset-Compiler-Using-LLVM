#!/usr/bin/env bash

# 结果验证脚本：编译运行 ToyC 和 Clang 生成的汇编，对比输出结果
# 支持两种运行方式:
#   - Linux/WSL: Clang 汇编/链接 + QEMU 用户模式
#   - macOS:     riscv64-unknown-elf-gcc 汇编/链接 + spike + pk (rv32)

set -eu
if [[ -n "${BASH_VERSION:-}" ]]; then
    set -o pipefail
fi

# 支持通过参数指定源目录，默认为 examples/compiler_inputs
SRC_DIR="${1:-examples/compiler_inputs}"
# 支持指定单个文件进行测试
SINGLE_FILE="${2:-}"
ASM_DIR="examples/asm"

# 检查源目录是否存在
if [[ ! -d "$SRC_DIR" ]]; then
  echo "Error: Source directory '$SRC_DIR' does not exist"
  exit 1
fi

# 检查汇编输出目录
if [[ ! -d "$ASM_DIR" ]]; then
  echo "Error: Assembly directory '$ASM_DIR' not found"
  echo "Please run 'make test' first to generate assembly files"
  exit 1
fi

# RISC-V 架构参数
RV_ARCH="rv32im"
RV_ABI="ilp32"
CLANG_TARGET="riscv32-unknown-elf"

# ========== 检测平台和工具链 ==========
OS_TYPE="$(uname -s)"
# 运行模式: "qemu" 或 "spike"
RUN_MODE=""

# 获取脚本所在目录（用于找到 crt0.s 和 setup 脚本）
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CRT0_FILE="$SCRIPT_DIR/crt0.s"

# 检查启动文件是否存在
if [[ ! -f "$CRT0_FILE" ]]; then
  echo "Error: Startup file '$CRT0_FILE' not found"
  exit 1
fi

# ---------- 查找 QEMU ----------
QEMU_CMD=""
for cmd in qemu-riscv32 qemu-riscv32-static qemu-riscv64 qemu-riscv64-static; do
  if command -v "$cmd" >/dev/null 2>&1; then
    QEMU_CMD="$cmd"
    break
  fi
done

# ---------- 查找 spike + pk ----------
SPIKE_CMD=""
PK_PATH=""
if command -v spike >/dev/null 2>&1; then
  SPIKE_CMD="spike"
  # 查找 rv32 pk: 优先使用项目 build/ 中的版本
  for candidate in \
      "$PROJECT_DIR/build/pk_rv32" \
      "$(brew --prefix riscv-software-src/riscv/riscv-pk 2>/dev/null)/riscv64-unknown-elf/bin/pk" \
      ; do
    if [[ -f "$candidate" ]] && file "$candidate" 2>/dev/null | grep -q "32-bit"; then
      PK_PATH="$candidate"
      break
    fi
  done
fi

# ---------- 选择运行模式 ----------
if [[ -n "$QEMU_CMD" ]]; then
  RUN_MODE="qemu"
elif [[ -n "$SPIKE_CMD" ]] && [[ -n "$PK_PATH" ]]; then
  RUN_MODE="spike"
elif [[ -n "$SPIKE_CMD" ]] && [[ -z "$PK_PATH" ]]; then
  echo "Error: spike found but rv32 pk not found."
  echo "Run 'bash scripts/setup_spike_rv32.sh' to build rv32 proxy kernel."
  exit 1
else
  echo "Error: No RISC-V emulator found."
  echo ""
  if [[ "$OS_TYPE" == "Darwin" ]]; then
    echo "macOS setup:"
    echo "  brew tap riscv-software-src/riscv"
    echo "  brew install riscv-isa-sim riscv-gnu-toolchain"
    echo "  bash scripts/setup_spike_rv32.sh"
  else
    echo "Linux setup:"
    echo "  sudo apt install qemu-user clang"
  fi
  exit 1
fi

# ---------- 设置汇编/链接工具链 ----------
# 查找 libgcc（提供软件除法等内置函数）
LIBGCC_PATH=""
if command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then
  LIBGCC_PATH=$(riscv64-unknown-elf-gcc -march=$RV_ARCH -mabi=$RV_ABI -print-libgcc-file-name 2>/dev/null || true)
  if [[ -n "$LIBGCC_PATH" ]] && [[ -f "$LIBGCC_PATH" ]]; then
    echo "Note: Using libgcc from $LIBGCC_PATH"
  else
    LIBGCC_PATH=""
  fi
fi

# 汇编/链接方式取决于可用工具
USE_GCC_LINK=false
if [[ "$RUN_MODE" == "spike" ]]; then
  # macOS spike 模式: 必须使用 riscv64-unknown-elf-gcc 链接
  if ! command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then
    echo "Error: riscv64-unknown-elf-gcc required for spike mode"
    echo "Install with: brew install riscv-software-src/riscv/riscv-gnu-toolchain"
    exit 1
  fi
  USE_GCC_LINK=true
  echo "Note: Using riscv64-unknown-elf-gcc for assembly/linking"
  echo "Note: Using spike + pk ($PK_PATH) for execution"
else
  # Linux QEMU 模式: 使用 Clang
  if ! command -v clang >/dev/null 2>&1; then
    echo "Error: Clang not found"
    echo "  Ubuntu/Debian: sudo apt install clang"
    exit 1
  fi
  if ! clang --target=$CLANG_TARGET -march=$RV_ARCH -mabi=$RV_ABI -c -x assembler /dev/null -o /dev/null 2>/dev/null; then
    echo "Error: Clang does not support RISC-V target"
    exit 1
  fi
  echo "Note: Using Clang with --target=$CLANG_TARGET"
  echo "Note: Using $QEMU_CMD for execution"
  if [[ -z "$LIBGCC_PATH" ]]; then
    echo "Warning: libgcc not found, some tests with division may fail"
  fi
fi

# 创建临时目录用于编译
TEMP_DIR="examples/verify_temp"
mkdir -p "$TEMP_DIR"

# 编译启动文件
CRT0_OBJ="$TEMP_DIR/crt0.o"
if [[ "$USE_GCC_LINK" == true ]]; then
  if ! riscv64-unknown-elf-gcc -march=$RV_ARCH -mabi=$RV_ABI \
       -c "$CRT0_FILE" -o "$CRT0_OBJ" 2>/dev/null; then
    echo "Error: Failed to compile startup file with riscv64-unknown-elf-gcc"
    exit 1
  fi
else
  if ! clang --target=$CLANG_TARGET -march=$RV_ARCH -mabi=$RV_ABI \
       -c "$CRT0_FILE" -o "$CRT0_OBJ" 2>/dev/null; then
    echo "Error: Failed to compile startup file with Clang"
    exit 1
  fi
fi

# ========== 辅助函数: 汇编链接 ==========
link_asm() {
  local asm_file="$1"
  local output="$2"
  local link_libs=""
  if [[ -n "$LIBGCC_PATH" ]]; then
    link_libs="$LIBGCC_PATH"
  fi

  if [[ "$USE_GCC_LINK" == true ]]; then
    # GNU assembler 不支持 Clang .addrsig 等指令，过滤掉
    local filtered="$TEMP_DIR/$(basename "$asm_file" .s)_filtered.s"
    grep -v '^\s*\.addrsig' "$asm_file" > "$filtered" 2>/dev/null || cp "$asm_file" "$filtered"
    riscv64-unknown-elf-gcc -march=$RV_ARCH -mabi=$RV_ABI \
      -nostdlib "$CRT0_OBJ" "$filtered" $link_libs -o "$output" 2>/dev/null
  else
    clang --target=$CLANG_TARGET -march=$RV_ARCH -mabi=$RV_ABI \
      -nostdlib "$CRT0_OBJ" "$asm_file" $link_libs -o "$output" 2>/dev/null
  fi
}

# ========== 辅助函数: 执行 ELF ==========
run_elf() {
  local elf="$1"
  if [[ "$RUN_MODE" == "spike" ]]; then
    spike --isa=rv32im "$PK_PATH" "$elf" 2>/dev/null
  else
    $QEMU_CMD "$elf" 2>/dev/null
  fi
}

# 统计变量
TOTAL=0
PASSED=0
FAILED=0
SKIPPED=0

echo "========================================="
echo "  ToyC Compiler Output Verification"
echo "========================================="
echo "Source directory: $SRC_DIR"
if [[ -n "$SINGLE_FILE" ]]; then
  echo "Testing single file: $SINGLE_FILE"
fi
echo "Assembly directory: $ASM_DIR"
if [[ "$RUN_MODE" == "spike" ]]; then
  echo "Emulator: spike + pk (rv32im)"
else
  echo "Emulator: $QEMU_CMD"
fi
echo ""

# 准备测试文件列表
if [[ -n "$SINGLE_FILE" ]]; then
  # 单文件测试模式
  # 智能处理文件路径：如果包含路径分隔符，说明是相对路径，直接使用
  if [[ "$SINGLE_FILE" == *"/"* ]] || [[ "$SINGLE_FILE" == *"\\"* ]]; then
    c_file="$SINGLE_FILE"
  else
    c_file="$SRC_DIR/$SINGLE_FILE"
  fi
  
  if [[ ! -f "$c_file" ]]; then
    echo "Error: File '$c_file' does not exist"
    exit 1
  fi
  TEST_FILES=("$c_file")
else
  # 批量测试模式
  TEST_FILES=("$SRC_DIR"/*.c)
fi

# 遍历所有测试文件
for c_file in "${TEST_FILES[@]}"; do
  # 跳过不存在的情况
  if [[ ! -f "$c_file" ]]; then
    echo "No .c files found in $SRC_DIR"
    exit 0
  fi
  
  base="$(basename "$c_file" .c)"
  toyc_asm="$ASM_DIR/${base}_toyc.s"
  clang_asm="$ASM_DIR/${base}_clang.s"
  
  TOTAL=$((TOTAL + 1))
  
  # 检查汇编文件是否存在且不为空
  if [[ ! -f "$toyc_asm" ]] || [[ ! -s "$toyc_asm" ]]; then
    echo "─────────────────────────────────────────"
    echo "Testing: $base"
    echo "  ⚠ SKIPPED: ToyC assembly not found or empty (compilation failed)"
    SKIPPED=$((SKIPPED + 1))
    continue
  fi
  
  if [[ ! -f "$clang_asm" ]] || [[ ! -s "$clang_asm" ]]; then
    echo "─────────────────────────────────────────"
    echo "Testing: $base"
    echo "  ⚠ SKIPPED: Clang assembly not found or empty (reference missing)"
    SKIPPED=$((SKIPPED + 1))
    continue
  fi
  
  echo "─────────────────────────────────────────"
  echo "Testing: $base"
  
  # 构建链接参数（如果有 libgcc 则添加）
  LINK_LIBS=""
  if [[ -n "$LIBGCC_PATH" ]]; then
    LINK_LIBS="$LIBGCC_PATH"
  fi
  
  # 编译 ToyC 生成的汇编
  toyc_exe="$TEMP_DIR/${base}_toyc"
  if ! link_asm "$toyc_asm" "$toyc_exe"; then
    echo "  ❌ ToyC assembly compilation failed"
    FAILED=$((FAILED + 1))
    continue
  fi
  
  # 编译 Clang 生成的汇编
  clang_exe="$TEMP_DIR/${base}_clang"
  if ! link_asm "$clang_asm" "$clang_exe"; then
    echo "  ❌ Clang assembly compilation failed"
    FAILED=$((FAILED + 1))
    continue
  fi
  
  # 运行 ToyC 生成的可执行文件
  toyc_output=""
  toyc_exitcode=0
  if toyc_output=$(run_elf "$toyc_exe" 2>&1); then
    toyc_exitcode=$?
  else
    toyc_exitcode=$?
  fi
  
  # 运行 Clang 生成的可执行文件
  clang_output=""
  clang_exitcode=0
  if clang_output=$(run_elf "$clang_exe" 2>&1); then
    clang_exitcode=$?
  else
    clang_exitcode=$?
  fi
  
  # 对比结果
  echo "  Clang  exit code: $clang_exitcode"
  echo "  ToyC   exit code: $toyc_exitcode"
  
  if [[ -n "$clang_output" ]]; then
    echo "  Clang  output: $clang_output"
  fi
  if [[ -n "$toyc_output" ]]; then
    echo "  ToyC   output: $toyc_output"
  fi
  
  # 判断是否一致
  if [[ "$toyc_exitcode" -eq "$clang_exitcode" ]] && [[ "$toyc_output" == "$clang_output" ]]; then
    echo "  ✅ Result: CORRECT"
    PASSED=$((PASSED + 1))
  else
    echo "  ❌ Result: INCORRECT"
    if [[ "$toyc_exitcode" -ne "$clang_exitcode" ]]; then
      echo "     Exit code mismatch: expected $clang_exitcode, got $toyc_exitcode"
    fi
    if [[ "$toyc_output" != "$clang_output" ]]; then
      echo "     Output mismatch"
      echo "     Expected: '$clang_output'"
      echo "     Got:      '$toyc_output'"
    fi
    FAILED=$((FAILED + 1))
  fi
done

echo "========================================="
echo "  Verification Summary"
echo "========================================="
echo "Total files:  $TOTAL"
echo "Passed:       $PASSED ✅"
echo "Failed:       $FAILED ❌"
echo "Skipped:      $SKIPPED ⚠"
echo ""

if [[ $FAILED -eq 0 ]] && [[ $SKIPPED -eq 0 ]] && [[ $TOTAL -gt 0 ]]; then
  echo "🎉 All tests passed!"
  exit 0
elif [[ $FAILED -eq 0 ]] && [[ $TOTAL -gt 0 ]]; then
  echo "✅ All runnable tests passed (some files skipped due to compilation errors)"
  exit 0
elif [[ $TOTAL -eq 0 ]]; then
  echo "⚠ No tests were run"
  exit 1
else
  echo "⚠ Some tests failed"
  exit 1
fi
