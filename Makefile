# ─── CMake 包装器 Makefile ──────────────────────────────────────
#
# 使用方式:
#   make              编译项目
#   make test         运行 36 个内置单元测试（同时保存 AST/IR/ASM 产物）
#   make generate-asm 批量生成 ToyC + Clang 汇编
#   make generate-ir  批量生成 ToyC + Clang LLVM IR
#   make generate-ast 批量生成 ToyC AST 输出
#   make verify       端到端验证：汇编 → 链接 → 模拟运行 → 对比结果
#   make debug FILE=01_minimal.c   单文件调试
#   make setup-spike  (macOS) 构建 rv32 proxy kernel 用于 spike 验证
#   make clean        清理构建与测试产物
#   make clean-test   仅清理测试产物（保留 build/）
#
# 支持平台:
#   - macOS (Apple Clang / Homebrew Clang) + CMake ≥ 3.16
#     验证依赖: spike + riscv-gnu-toolchain (通过 Homebrew)
#   - Linux / WSL (Ubuntu) + CMake ≥ 3.16 + g++ ≥ 13 (C++20)
#     验证依赖: clang + qemu-user
#
# macOS 安装依赖:
#     brew install cmake
#     brew tap riscv-software-src/riscv
#     brew install riscv-isa-sim riscv-gnu-toolchain
#     make setup-spike    # 一键构建 rv32 pk
#
# Linux/WSL 安装依赖:
#     sudo apt install build-essential cmake clang qemu-user
# ─────────────────────────────────────────────────────────────────

BUILD_DIR := build
SRC_DIR   := examples/compiler_inputs

.PHONY: all build test generate-asm generate-ir generate-ast verify debug setup-spike clean clean-test rebuild help

all: build

# ---------- 编译 ----------
build:
	@cmake -S . -B $(BUILD_DIR)
	@cmake --build $(BUILD_DIR) --parallel

# ---------- 内置测试 ----------
test: build
	@bash scripts/generate_asm.sh $(SRC_DIR) > /dev/null
	@bash scripts/generate_ir.sh $(SRC_DIR) > /dev/null
	@bash scripts/generate_ast.sh $(SRC_DIR) > /dev/null
	@./$(BUILD_DIR)/toyc_test $(SRC_DIR)

# ---------- 批量汇编生成 ----------
generate-asm: build
	@bash scripts/generate_asm.sh $(SRC_DIR) > /dev/null

# ---------- 批量 IR 生成 ----------
generate-ir: build
	@bash scripts/generate_ir.sh $(SRC_DIR) > /dev/null

# ---------- 批量 AST 生成 ----------
generate-ast: build
	@bash scripts/generate_ast.sh $(SRC_DIR) > /dev/null

# ---------- 端到端验证（Linux: QEMU, macOS: spike+pk） ----------
verify: build
	@bash scripts/generate_asm.sh $(SRC_DIR) > /dev/null
	@bash scripts/generate_ir.sh $(SRC_DIR) > /dev/null
	@bash scripts/verify_output.sh $(SRC_DIR)

# ---------- macOS: 构建 rv32 proxy kernel ----------
setup-spike:
	@bash scripts/setup_spike_rv32.sh

# ---------- 单文件调试 ----------
# 用法: make debug FILE=01_minimal.c
debug: build
	@if [ -z "$(FILE)" ]; then \
	    echo "Usage: make debug FILE=<filename.c>"; \
	    echo "Example: make debug FILE=01_minimal.c"; \
	    exit 1; \
	fi
	@bash scripts/verify_debug.sh $(SRC_DIR) $(FILE)

# ---------- 清理 ----------
clean:
	@rm -rf $(BUILD_DIR) build-linux examples/asm examples/ir examples/ast examples/debug examples/verify_temp 2>/dev/null; true

clean-test:
	@rm -rf examples/asm examples/ir examples/ast examples/debug examples/verify_temp
	@echo "Cleaned generated output directories."

rebuild: clean build

# ---------- 便捷: 单文件编译 ----------
%.s: $(SRC_DIR)/%.c build
	@./$(BUILD_DIR)/toyc $< --asm

%.ll: $(SRC_DIR)/%.c build
	@./$(BUILD_DIR)/toyc $< --ir

# ---------- 帮助 ----------
help:
	@echo "ToyC Compiler - Makefile Targets"
	@echo "================================"
	@echo "  make              Build the project"
	@echo "  make test         Run 36 built-in unit tests (saves AST/IR/ASM)"
	@echo "  make generate-asm Generate ToyC + Clang assembly"
	@echo "  make generate-ir  Generate ToyC + Clang LLVM IR"
	@echo "  make generate-ast Generate ToyC AST output"
	@echo "  make verify       End-to-end verify (asm → link → emulate → diff)"
	@echo "  make debug FILE=xx.c  Single-file debug (AST/IR/ASM + verify)"
	@echo "  make setup-spike  (macOS) Build rv32 proxy kernel for spike"
	@echo "  make clean        Remove build/ and generated outputs under examples/"
	@echo "  make clean-test   Remove generated outputs under examples/ only (keep build/)"
	@echo "  make rebuild      Clean then build"
	@echo "  make help         This message"
