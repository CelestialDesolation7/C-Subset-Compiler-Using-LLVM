# ToyC - C 语言子集编译器

## 📖 目录

- [1 项目简介](#1-项目简介)
- [2 核心技术与亮点](#2-核心技术与亮点)
- [3 支持的语言特性](#3-支持的语言特性)
- [4 快速开始](#4-快速开始)
- [5 使用方法](#5-使用方法)
- [6 测试与验证](#6-测试与验证)
- [7 测试用例说明](#7-测试用例说明)
- [8 输出示例](#8-输出示例)
- [9 项目结构](#9-项目结构)
- [10 技术文档](#10-技术文档)

---

## 1 项目简介

ToyC 是一个基于现代编译技术实现的 **C 语言子集编译器**，采用多阶段编译架构，支持将 C 代码编译为 **LLVM IR** 和 **RISC-V 汇编代码**。

### 1.1 编译流程

```
C 源代码 → 词法分析 → 语法分析 → AST → IRBuilder → ir::Module → 寄存器分配 → RISC-V 汇编
```

### 1.2 技术栈

- **语言**: C++20
- **构建系统**: CMake (≥ 3.16) + Unix Makefiles
- **支持平台**: macOS (Apple Clang / Homebrew Clang)、Linux、WSL
- **目标架构**: RISC-V 32-bit (RV32I)
- **中间表示**: 结构化 LLVM IR (SSA 形式)，具有完整的 Opcode 枚举和类型化指令模型
- **寄存器分配**: 线性扫描算法
    （原论文：https://web.cs.ucla.edu/~palsberg/course/cs132/linearscan.pdf）
    （关于本项目中实现的解释：[线性扫描算法核心思路.md](docs/线性扫描算法核心思路.md)）

---

## 2 核心技术与亮点

### 2.1 完整的编译器前端

#### 2.1.1 词法分析器 (Lexer)
- **手工实现**的高效词法分析器
- 支持单字符和多字符运算符识别
- 完整的关键字、标识符、数字字面量处理
- 支持单行 `//` 和多行 `/* */` 注释

#### 2.1.2 语法分析器 (Parser)
- **递归下降**解析算法
- 完整的表达式优先级处理
- 支持复杂控制流语句嵌套
- 作用域管理和符号表维护

#### 2.1.3 抽象语法树 (AST)
- 面向对象的节点设计
- 支持语法树可视化输出
- 类型检查和语义分析

### 2.2 结构化 IR 模型

采用类似 LLVM 官方项目的 **结构化指令模型**，通过 `Opcode` 枚举和类型化 `Instruction` 类实现：

- **`ir::Opcode` 枚举**: 明确定义所有指令类型 (`Alloca`, `Load`, `Store`, `Add`, `Sub`, `Mul`, `SDiv`, `SRem`, `ICmp`, `Br`, `CondBr`, `Ret`, `RetVoid`, `Call`)
- **`ir::Operand` 类**: 类型安全的操作数 (`VReg`, `Imm`, `Label`, `BoolLit`)
- **`ir::Instruction` 工厂方法**: 如 `makeAlloca()`, `makeBinOp()`, `makeICmp()` 等
- **基于 Opcode 的查询接口**: `defReg()`, `useRegs()`, `isTerminator()`, `branchTargets()` — **无需正则表达式或字符串匹配**
- **`ir::BasicBlock` / `ir::Function` / `ir::Module`**: 完整的 CFG 构建和管理

#### 2.2.1 IRBuilder
- AST → 结构化 IR 的直接转换
- 作用域栈管理
- **短路求值**优化（逻辑运算符 `&&`, `||`）
- 函数调用约定实现

#### 2.2.2 IRParser
- 文本 LLVM IR → 结构化 `ir::Module` 的解析器
- 支持从 `.ll` 文件导入

### 2.3 寄存器分配算法

实现了经典的 **线性扫描寄存器分配算法** (Linear Scan Register Allocation)，直接操作结构化 IR：

#### 2.3.1 核心特性
- **活跃区间计算**: 通过 `ir::Instruction::defReg()` / `useRegs()` 精确分析变量生命周期，无需正则匹配
- **物理寄存器映射**: 符合 RISC-V 调用约定
    - `a0-a7`: 参数寄存器（最高分配优先级）
    - `t2-t6`: 临时寄存器
    - `s1-s11`: 保存寄存器
    - `t0/t1`: 溢出专用临时寄存器
- **溢出处理**: 自动栈分配和加载/存储生成
- **参数处理**: 支持多参数函数调用（前 8 个通过寄存器，其余通过栈）

#### 2.3.2 两项关键优化

在经典线性扫描算法基础上，本项目实现了两项优化，显著减少冗余指令并提升寄存器利用率：

1. **Caller-saved 按需保存**：函数调用点（`genCall`）不再无条件保存所有 caller-saved 寄存器，而是通过活跃区间查询 `interval.contains(callPosUse)` 仅保存在调用点仍然活跃的寄存器。`callSaveSize_` 也改为按每个调用点分别计算活跃寄存器数量后取最大值，缩减不必要的栈帧空间。

2. **Active/Inactive 转换（区间空洞感知）**：引入 `inactive` 列表和 `inHole()` 判定。当活跃区间在当前位置存在空洞（多段 `LiveRange` 之间的间隙）时，将其从 `active` 移至 `inactive` 而非释放寄存器。当区间在后续位置恢复活跃时，移回 `active`。这避免了控制流分支导致的不必要溢出。

#### 2.3.3 算法优势
- **时间复杂度**: O(n log n)，远优于图着色算法
- **空间效率**: 高效利用寄存器，减少内存访问
- **工业级实现**: 被广泛应用于 JVM、V8 等生产环境

详细算法说明见：[docs/线性扫描算法核心思路.md](docs/线性扫描算法核心思路.md)

### 2.4 RISC-V 代码生成

基于 **Opcode 分派**的代码生成器，通过 `switch(inst.opcode)` 实现指令级分发：

#### 2.4.1 支持的指令集
- **算术运算**: `add`, `addi`, `sub`, `mul`, `div`, `rem`
- **比较运算**: `slt`, `seqz`, `snez`
- **控制流**: `beq`, `bne`, `blt`, `bge`, `bgt`, `ble`, `j`, `ret`
- **内存访问**: `lw`, `sw`, `lb`, `sb`

#### 2.4.2 优化技术
- **比较-分支融合**: `icmp + condBr` 合并为单条 RISC-V 分支指令
- **立即数优化**: `add %x, imm` → `addi`
- **占位符栈帧**: 延迟计算栈大小，支持多返回路径

#### 2.4.3 调用约定
完全符合 **RISC-V ABI** 规范：
- 参数传递：`a0-a7` (前 8 个参数)
- 返回值：`a0`
- 返回地址：`ra`
- 栈指针：`sp`
- 帧指针：`s0`

### 2.5 多种输出模式

支持灵活的编译输出：
- **AST**: 树形结构可视化
- **LLVM IR**: 标准 LLVM IR 格式
- **Assembly**: RISC-V 汇编代码
- **All**: 同时输出以上所有内容

---

## 3 支持的语言特性

ToyC 是 C 语言的一个子集。以下从文法定义、词法规则和语义约束三个层面完整描述该语言。

### 3.1 文法定义 (EBNF)

以下采用扩展巴科斯-瑙尔范式 (EBNF) 描述 ToyC 的上下文无关文法。

- `{ X }` 表示 X 重复零次或多次
- `[ X ]` 表示 X 可选（零次或一次）
- `|` 表示选择
- 终结符用双引号括起

```ebnf
(* ==================== 编译单元 ==================== *)
CompUnit     ::= { FuncDef }

(* ==================== 函数定义 ==================== *)
FuncDef      ::= FuncType IDENT "(" [ FuncParams ] ")" Block
FuncType     ::= "int" | "void"
FuncParams   ::= "int" IDENT { "," "int" IDENT }

(* ==================== 语句块与语句 ==================== *)
Block        ::= "{" { BlockItem } "}"
BlockItem    ::= Decl | Stmt

Decl         ::= "int" IDENT "=" Expr { "," IDENT "=" Expr } ";"

Stmt         ::= Block
                             | "if" "(" Expr ")" Stmt [ "else" Stmt ]
                             | "while" "(" Expr ")" Stmt
                             | "return" [ Expr ] ";"
                             | "break" ";"
                             | "continue" ";"
                             | IDENT "=" Expr ";"
                             | Expr ";"
                             | ";"

(* ==================== 表达式 ==================== *)
Expr         ::= LOrExpr
LOrExpr      ::= LAndExpr  { "||" LAndExpr }
LAndExpr     ::= RelExpr   { "&&" RelExpr }
RelExpr      ::= AddExpr   { RelOp AddExpr }
AddExpr      ::= MulExpr   { ( "+" | "-" ) MulExpr }
MulExpr      ::= UnaryExpr { ( "*" | "/" | "%" ) UnaryExpr }
UnaryExpr    ::= ( "+" | "-" | "!" ) UnaryExpr
                             | PrimaryExpr
PrimaryExpr  ::= NUMBER
                             | IDENT
                             | IDENT "(" [ Expr { "," Expr } ] ")"
                             | "(" Expr ")"

RelOp        ::= "<" | ">" | "<=" | ">=" | "==" | "!="
```

### 3.2 词法规则

#### 3.2.1 终结符

| 终结符 | 正则表达式 / 说明 |
|--------|-------------------|
| `IDENT` | `[_A-Za-z][_A-Za-z0-9]*`　（与关键字不冲突时识别为标识符） |
| `NUMBER` | `[0-9]+`　（十进制非负整数字面量；负数由一元 `-` 运算符构成） |

#### 3.2.2 关键字（保留字）

以下标识符被保留为关键字，不能用作变量名或函数名：

```
int   void   if   else   while   return   break   continue
```

#### 3.2.3 运算符与分隔符

| 类别 | 符号 |
|------|------|
| 算术运算符 | `+`　`-`　`*`　`/`　`%` |
| 关系运算符 | `<`　`>`　`<=`　`>=`　`==`　`!=` |
| 逻辑运算符 | `&&`　`\|\|`　`!` |
| 赋值运算符 | `=` |
| 分隔符 | `(`　`)`　`{`　`}`　`;`　`,` |

#### 3.2.4 空白与注释

- **空白字符**：空格、制表符 `\t`、换行符 `\n` 等在词法分析阶段被跳过
- **单行注释**：以 `//` 开始，到最近的换行符前结束
- **多行注释**：以 `/*` 开始，以最近的 `*/` 结束

### 3.3 语义约束

#### 3.3.1 程序结构

- 程序由零个或多个函数定义组成
- 程序必须包含一个名为 `main`、参数列表为空、返回类型为 `int` 的函数作为入口点

#### 3.3.2 数据类型

| 类型 | 说明 |
|------|------|
| `int` | 32 位有符号整数，用于变量声明、函数参数和函数返回值 |
| `void` | 仅用于表示函数无返回值，不能声明 `void` 类型的变量 |

#### 3.3.3 函数

- 函数只能声明在**全局作用域**中，不能嵌套定义
- 函数名称必须**唯一**
- 函数参数类型统一为 `int`，支持零个或多个参数
- 返回类型为 `int` 或 `void`
- 函数**不能**作为值来存储、传递或参与运算
- 函数调用必须出现在被调函数声明**之后**（支持函数内部调用自身，即递归）
- 返回类型为 `int` 的函数须在每条可能的执行路径上通过 `return` 语句返回一个 `int` 值
- 返回类型为 `void` 的函数可以不包含 `return` 语句；如果包含，则 `return` 后不能有返回值

#### 3.3.4 变量声明

- 仅支持 `int` 类型的局部变量声明
- 每条声明语句可通过逗号声明多个变量（如 `int a = 1, b = 2;`），每个变量**必须**带有初始化表达式
- 变量的使用必须发生在声明之后

#### 3.3.5 作用域

- 变量的生命周期从声明点开始，到所在作用域结束时结束
- 函数形参在整个函数体内可见
- 语句块 `{ ... }` 创建新的作用域
- 内层作用域的同名变量会**遮蔽**（shadow）外层作用域的同名变量

#### 3.3.6 语句

| 语句 | 语法 | 说明 |
|------|------|------|
| 语句块 | `{ ... }` | 由大括号包围的语句序列，创建新作用域 |
| 空语句 | `;` | 仅包含分号 |
| 表达式语句 | `Expr ;` | 表达式求值后丢弃结果 |
| 赋值语句 | `IDENT = Expr ;` | 将表达式的值赋给已声明的变量 |
| 变量声明 | `int IDENT = Expr ;` | 声明并初始化局部变量 |
| 条件分支 | `if (Expr) Stmt [else Stmt]` | 条件为真执行 then 分支，否则执行 else 分支（可选） |
| 循环 | `while (Expr) Stmt` | 条件为真时重复执行循环体 |
| 跳出循环 | `break ;` | 立即退出最内层 `while` 循环（只能出现在循环中） |
| 继续迭代 | `continue ;` | 跳过本次循环体的剩余部分，进入下次迭代（只能出现在循环中） |
| 返回 | `return [Expr] ;` | 从函数返回，可选携带返回值 |

> **注意**：没有返回值的函数调用表达式不能作为 `if` / `while` 的条件或赋值语句的右值。

#### 3.3.7 表达式与运算符

##### 3.3.7.1 运算符优先级（从低到高）

| 优先级 | 运算符 | 结合性 | 说明 |
|:------:|--------|:------:|------|
| 1 | `\|\|` | 左结合 | 逻辑或 |
| 2 | `&&` | 左结合 | 逻辑与 |
| 3 | `<`　`>`　`<=`　`>=`　`==`　`!=` | 左结合 | 关系比较 |
| 4 | `+`　`-` | 左结合 | 加法 / 减法 |
| 5 | `*`　`/`　`%` | 左结合 | 乘法 / 除法 / 取模 |
| 6 | `+`　`-`　`!`（一元） | 右结合 | 正号 / 负号 / 逻辑非 |

##### 3.3.7.2 表达式类型

| 表达式 | 示例 | 说明 |
|--------|------|------|
| 整数字面量 | `42` | 十进制整数常量 |
| 变量引用 | `x` | 引用已声明的变量 |
| 函数调用 | `f(a, b)` | 调用已声明的函数，参数按值传递 |
| 括号表达式 | `(a + b)` | 覆盖默认优先级 |
| 一元运算 | `-x`、`!flag` | 对单个操作数施加运算 |
| 二元运算 | `a + b`、`x == y` | 对两个操作数施加运算 |

##### 3.3.7.3 求值规则

- **短路求值**：`&&` 和 `||` 遵守短路计算规则——`a && b` 中若 `a` 为假则不求值 `b`；`a || b` 中若 `a` 为真则不求值 `b`
- **真值判定**：非零值视为「真」，零视为「假」
- **除法约束**：除数不能为零（运行时行为未定义）

---

## 4 快速开始

### 4.1 环境要求

#### 4.1.1 macOS

- macOS 13+ (Ventura 及以上)
- Xcode Command Line Tools (Apple Clang, 支持 C++20)
- CMake ≥ 3.16
- clang（可选，用于汇编对比；系统自带或 Homebrew 均可）
- 端到端验证额外依赖: spike + riscv-gnu-toolchain (通过 Homebrew)

#### 4.1.2 Linux / WSL

- CMake ≥ 3.16 + g++ ≥ 13（C++20）
- clang（含 RISC-V 后端）
- qemu-user（RISC-V 用户态模拟器，用于 `make verify`）
- riscv64-unknown-elf-gcc（可选，提供 libgcc 软件除法支持）

### 4.2 安装依赖

```bash
# macOS (使用 Homebrew)
xcode-select --install      # 安装 Xcode Command Line Tools
brew install cmake           # 安装 CMake

# 端到端验证依赖 (make verify)
brew tap riscv-software-src/riscv
brew install riscv-isa-sim riscv-gnu-toolchain
make setup-spike             # 一键构建 rv32 proxy kernel
```

```bash
# Linux / WSL (Ubuntu)
sudo apt update
sudo apt install build-essential cmake clang qemu-user
# 可选：安装 RISC-V 工具链以获取 libgcc
sudo apt install gcc-riscv64-unknown-elf
```

### 4.3 编译项目

```bash
# macOS / Linux / WSL 中执行
make

# 或从 Windows PowerShell 通过 WSL 调用
wsl make
```

编译完成后，可执行文件位于 `build/toyc`（编译器）、`build/toyc_test`（测试工具）和 `build/ra_debug`（寄存器分配调试工具）。

---

## 5 使用方法

### 5.1 命令行接口

```
用法: toyc <input.[c|tc|ll]> [options]

选项:
    --ast         输出抽象语法树
    --ir          输出 LLVM IR
    --asm         输出 RISC-V 汇编（默认）
    --all         输出 AST + IR + 汇编
    -o <file>     将汇编写入指定文件
```

### 5.2 使用示例

```bash
# 1. 编译到 RISC-V 汇编（默认模式）
./build/toyc examples/compiler_inputs/01_minimal.c

# 2. 生成 LLVM IR
./build/toyc examples/compiler_inputs/05_function_call.c --ir

# 3. 查看 AST
./build/toyc examples/compiler_inputs/03_if_else.c --ast

# 4. 同时查看 AST + IR + 汇编
./build/toyc examples/compiler_inputs/09_recursion.c --all

# 5. 将汇编输出到文件
./build/toyc examples/compiler_inputs/09_recursion.c -o output.s

# 6. 从 .ll 文件生成汇编（支持 IR 输入）
./build/toyc test/ir/01_minimal_toyc.ll --asm
```

### 5.3 Makefile 便捷目标

```bash
# 编译单个文件到汇编（stdout）
make 01_minimal.s

# 编译单个文件到 LLVM IR（stdout）
make 01_minimal.ll
```

---

## 6 测试与验证

ToyC 提供四层测试体系——从快速的内置单元测试到完整的端到端验证。
所有功能在 macOS 和 Linux/WSL 上均可运行。

- **Linux/WSL**: 使用 Clang 汇编/链接 + QEMU 用户模式运行
- **macOS**: 使用 riscv64-unknown-elf-gcc 汇编/链接 + spike + pk (rv32) 运行

### 6.1 内置单元测试

对所有 37 个测试用例执行完整编译流水线（词法 → 语法 → AST → IR → 寄存器分配 → RISC-V 汇编），验证各阶段无异常，并同时将 AST、IR、汇编产物保存到 `test/` 目录：

```bash
make test
```

输出示例：
```
Testing: 01_minimal.c ... OK
Testing: 02_assignment.c ... OK
...
Testing: 36_test_while.c ... OK
Testing: 37_test_regalloc_opts.c ... OK

=== Results: 37/37 passed ===
```

### 6.2 批量生成汇编 / IR / AST

批量对所有测试用例同时调用 ToyC 和 Clang，生成汇编、IR 或 AST 以供人工对比分析。

```bash
# 批量生成 RISC-V 汇编（ToyC + Clang）
make generate-asm
# 输出到 test/asm/：<base>_toyc.s 和 <base>_clang.s

# 批量生成 LLVM IR（ToyC + Clang）
make generate-ir
# 输出到 test/ir/：<base>_toyc.ll 和 <base>_clang.ll

# 批量生成 AST
make generate-ast
# 输出到 test/ast/：<base>_toyc.ast
```

### 6.3 端到端验证

对每个测试用例，分别将 ToyC 和 Clang 生成的汇编**链接为 RISC-V ELF** 并模拟执行，对比两者的退出码和输出，确保 ToyC 生成的代码行为与 Clang 完全一致。

```bash
# macOS 首次使用需先构建 rv32 proxy kernel
make setup-spike

# 一键验证（自动生成汇编 + 验证，汇编生成过程静默）
make verify
```

验证流程：
```
# Linux/WSL:
ToyC ASM → Clang 汇编器 → ELF → QEMU 执行 → 退出码 A

# macOS:
ToyC ASM → riscv64-unknown-elf-gcc → ELF → spike + pk 执行 → 退出码 A

# 两种平台均:
Clang ASM → 同上链接/执行 → 退出码 B
比较 A == B → ✅ PASS / ❌ FAIL
```

> **说明**: 链接使用自定义启动代码 `scripts/crt0.s`（调用 `main` 后执行 `ecall` 退出），无需标准 C 库。

### 6.4 单文件调试模式

对单个文件输出所有中间产物（AST → IR → ASM），同时生成 Clang 参考输出，并自动进行端到端验证：

```bash
make debug FILE=01_minimal.c
```

调试模式流程（6 步）：
1. 生成 AST 并打印
2. 生成 LLVM IR 并保存
3. 生成 RISC-V 汇编
4. 生成 Clang 参考 IR 和汇编
5. 复制产物到 `test/asm/` 和 `test/ir/` 目录
6. 调用 `verify_output.sh` 执行端到端验证

### 6.5 从 Windows PowerShell 调用 (WSL)

在 Windows 环境开发时，所有 make 指令通过 `wsl` 前缀调用：

```powershell
wsl make              # 编译
wsl make test         # 运行单元测试
wsl make verify       # 端到端验证
wsl make debug FILE=01_minimal.c
```

### 6.6 清理

```bash
make clean       # 清理 build/ 和 test/ 目录
make clean-test  # 仅清理 test/ 目录（保留 build/）
make rebuild     # 清理后重新编译
```

---

## 7 测试用例说明

项目包含 **37 个测试用例**，覆盖从基础语法到复杂控制流的各种场景：

### 7.1 基础语法（01–15）

| 文件                         | 测试功能             | 预期退出码 |
| ---------------------------- | -------------------- | ---------- |
| `01_minimal.c`               | 最小程序（空 main）  | 0          |
| `02_assignment.c`            | 变量赋值运算         | 3          |
| `03_if_else.c`               | if-else 条件分支     | 4          |
| `04_while_break.c`           | while 循环 + break   | 5          |
| `05_function_call.c`         | 基本函数定义与调用   | 7          |
| `06_continue.c`              | continue 跳过迭代    | 8          |
| `07_scope_shadow.c`          | 块作用域变量遮蔽     | 1          |
| `08_short_circuit.c`         | 逻辑短路求值 `&&` `\|\|` | 211    |
| `09_recursion.c`             | 递归函数（阶乘）     | 120        |
| `10_void_fn.c`               | void 函数定义与调用  | 0          |
| `11_precedence.c`            | 运算符优先级         | 14         |
| `12_division_check.c`        | 整数除法             | 2          |
| `13_scope_block.c`           | 块内局部变量作用域   | 8          |
| `14_nested_if_while.c`       | 嵌套 if + while      | 6          |
| `15_multiple_return_paths.c` | 多路径 return        | 62         |

### 7.2 进阶功能（16–20）

| 文件                         | 测试功能             |
| ---------------------------- | -------------------- |
| `16_complex_syntax.c`        | 复杂语法含注释解析   |
| `17_complex_expressions.c`   | 阶乘 + 斐波那契 + 嵌套调用 |
| `18_many_variables.c`        | 大量局部变量（寄存器溢出） |
| `19_many_arguments.c`        | 8/16 参数函数（栈传递） |
| `20_comprehensive.c`         | 综合测试：递归 + 循环 + 多函数 |

### 7.3 回归测试（21–36）

| 文件                           | 测试功能                   |
| ------------------------------ | -------------------------- |
| `21_test_break_continue.c`     | break + continue 组合      |
| `22_test_call.c`               | 函数调用返回值             |
| `23_test_complex.c`            | 复合表达式 + 块作用域      |
| `24_test_fact.c`               | 递归阶乘                   |
| `25_test_fib.c`                | 递归斐波那契               |
| `26_test_if.c`                 | if-else 分支               |
| `27_test_logic.c`              | 逻辑表达式 `&& \|\| !`    |
| `28_test_logical_multiple.c`   | 多条件逻辑组合             |
| `29_test_mix_expr.c`           | 混合表达式 + 一元运算      |
| `30_test_modulo.c`             | 取模运算                   |
| `31_test_multiple_funcs.c`     | 多函数定义与调用           |
| `32_test_nested_block.c`       | 嵌套块作用域               |
| `33_test_nested_loops.c`       | 嵌套 while 循环            |
| `34_test_unary.c`              | 一元运算符 `-` 和 `!`     |
| `35_test_void.c`               | void 函数                  |
| `36_test_while.c`              | while + break 循环控制     |

### 7.4 寄存器分配优化验证（37）

| 文件                             | 测试功能                                     | 预期退出码 |
| -------------------------------- | -------------------------------------------- | ---------- |
| `37_test_regalloc_opts.c`        | caller-saved 按需保存 + active/inactive 转换 | 228        |

---

## 8 输出示例

以 `01_minimal.c` 为例：

```c
// 01_minimal.c
int main() {
        return 0;
}
```

### 8.1 AST 输出 (`--ast`)

```
=== AST ===
FuncDef: int main()
    Block:
        Return:
            Number: 0
```

### 8.2 LLVM IR 输出 (`--ir`)

```
=== LLVM IR ===
define i32 @main() {
entry:
    ret i32 0
}
```

### 8.3 RISC-V 汇编输出 (`--asm`)

```
=== RISC-V Assembly ===
    .text
    .globl main
main:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)
    addi s0, sp, 16
    li a0, 0
    lw ra, 12(sp)
    lw s0, 8(sp)
    addi sp, sp, 16
    ret
```

---

## 9 项目结构

```
C-SubsetCompilerUsingLLVM/
├── CMakeLists.txt                  # CMake 构建配置
├── Makefile                        # 包装器：build / test / verify / debug / clean
├── README.md                       # 本文档
│
├── src/                            # 源代码
│   ├── include/                    # 头文件（工作目录，开发时直接修改此处）
│   │   ├── token.h                 #   Token 类型枚举
│   │   ├── lexer.h                 #   词法分析器
│   │   ├── ast.h                   #   AST 节点定义
│   │   ├── parser.h                #   语法分析器
│   │   ├── ir.h                    #   结构化 IR 模型
│   │   ├── ir_builder.h            #   IRBuilder（AST → ir::Module）
│   │   ├── ir_parser.h             #   IRParser（LLVM IR 文本 → ir::Module）
│   │   ├── reg_alloc.h             #   线性扫描寄存器分配器
│   │   └── riscv_codegen.h         #   RISC-V 代码生成器
│   ├── main.cpp                    # 主程序入口（CLI 处理）
│   ├── lexer.cpp                   # 词法分析实现
│   ├── ast.cpp                     # AST 打印实现
│   ├── parser.cpp                  # 语法分析实现
│   ├── ir.cpp                      # IR 模型实现
│   ├── ir_builder.cpp              # IRBuilder 实现（AST → IR 转换）
│   ├── ir_parser.cpp               # IRParser 实现（.ll 文本 → IR 结构）
│   ├── reg_alloc.cpp               # 寄存器分配器实现
│   ├── riscv_codegen.cpp           # RISC-V 代码生成实现
│   ├── unified_test.cpp            # 统一测试程序
│   └── ra_debug.cpp                # 寄存器分配调试工具
│
├── scripts/                        # 构建和测试脚本（macOS / Linux / WSL）
│   ├── crt0.s                      #   RISC-V 启动代码
│   ├── generate_asm.sh             #   批量生成 ToyC + Clang 汇编
│   ├── generate_ir.sh              #   批量生成 ToyC + Clang LLVM IR
│   ├── generate_ast.sh             #   批量生成 ToyC AST 输出
│   ├── verify_output.sh            #   端到端验证
│   ├── verify_debug.sh             #   单文件调试模式
│   ├── setup_spike_rv32.sh         #   (macOS) 一键构建 rv32 proxy kernel
│   └── test_instr.sh               #   指令测试命令参考
│
├── examples/                       # 测试用例
│   └── compiler_inputs/            #   37 个 .c 测试文件
│       ├── 01_minimal.c            #   最小程序
│       ├── ...                     #   ...
│       ├── 36_test_while.c         #   while 循环
│       └── 37_test_regalloc_opts.c #   寄存器分配优化验证
│
├── docs/                           # 技术文档（7 篇）
│   ├── 编译流程详解.md
│   ├── 从调用链理解的寄存器分配流程.md
│   ├── 从调用链理解的目标代码生成.md
│   ├── 汇编到验证完整流程.md
│   ├── 寄存器分配与代码生成详解.md
│   ├── 线性扫描算法核心思路.md
│   └── 重构后项目错误修复日志.md
│
├── assets/                         # 文档图片资源
└── build/                          # 构建产物（自动生成）
        ├── toyc                        #   编译器主程序
        ├── toyc_test                   #   统一测试程序
        └── ra_debug                    #   寄存器分配调试工具
```

---

## 10 技术文档

详细的技术说明位于 `docs/` 目录：

| 文档 | 说明 |
|------|------|
| [编译流程详解](docs/编译流程详解.md) | 整体编译流水线：词法分析 → 语法分析 → IR 生成 → 寄存器分配 → 代码生成 |
| [从调用链理解的寄存器分配流程](docs/从调用链理解的寄存器分配流程.md) | 活跃性分析 + 线性扫描寄存器分配的完整调用链追踪 |
| [从调用链理解的目标代码生成](docs/从调用链理解的目标代码生成.md) | RISC-V 汇编生成的完整调用链追踪 |
| [汇编到验证完整流程](docs/汇编到验证完整流程.md) | 从汇编输出到 QEMU 验证的端到端流程 |
| [寄存器分配与代码生成详解](docs/寄存器分配与代码生成详解.md) | 寄存器分配与代码生成的算法细节 |
| [线性扫描算法核心思路](docs/线性扫描算法核心思路.md) | 线性扫描寄存器分配核心算法解释 |
| [重构后项目错误修复日志](docs/重构后项目错误修复日志.md) | 重构后修复的 11 个代码生成 / IR 构建错误记录 |

