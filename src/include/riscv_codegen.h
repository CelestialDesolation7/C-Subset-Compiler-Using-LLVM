#pragma once
#include "ir.h"
#include "reg_alloc.h"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

namespace toyc {

// RISC-V32 代码生成器：从结构化 IR（ir::Module）生成 RISC-V 汇编文本
// 核心流程：
//   1. precomputeAllocations — 对每个函数执行线性扫描寄存器分配
//   2. generateFunction      — 按函数生成汇编（prologue → 指令 → epilogue）
//   3. 占位符替换            — 函数结束后回填实际栈帧大小
class RISCVCodeGen {
#pragma region 公有接口
  public:
    RISCVCodeGen();
    // 主入口：生成整个模块的 RISC-V 汇编
    std::string generate(ir::Module &module);
#pragma endregion

#pragma region 私有状态
  private:
    RegInfo regInfo_; // 目标架构寄存器信息
    std::map<std::string, std::unique_ptr<LinearScanAllocator>> funcAllocators_; // 函数名 → 分配器

    // -------- 每个函数的运行时状态 --------
    std::string currentFunction_; // 当前处理的函数名
    bool isMainFunction_ = false; // 是否为 main 函数
    bool hasReturn_ = false;      // 当前函数是否已有 return
    std::string output_;          // 汇编输出缓冲区
    std::string lastDefRegName_; // resolveDef 返回的寄存器名（供 spillDefIfNeeded 使用）

    // -------- alloca/栈偏移 --------
    std::map<int, int> allocaOffsets_; // alloca vreg → 栈偏移
    int stackOffset_ = 0;              // 已分配的局部变量栈空间
    int totalStackSize_ = 0;           // 函数总栈帧大小
    int frameOverhead_ = 0;            // ra + s0 + callee-saved 字节数
    int callSaveSize_ = 0;             // 函数调用时 caller-saved 保存区字节数
    int callArgAreaSize_ = 0;          // 超过 8 个参数时的出栈参数区字节数

    // -------- 比较信息延迟合并到分支（branch fusion） --------
    struct CmpInfo {
        ir::CmpPred pred;           // 比较谓词
        std::string lhsReg, rhsReg; // 已解析的左右操作数寄存器名
    };
    std::unordered_map<int, CmpInfo> cmpMap_; // vreg → CmpInfo
#pragma endregion

#pragma region 私有方法

    // -------- 预计算寄存器分配 --------
    // 对模块中每个函数执行线性扫描，结果保存到 funcAllocators_
    void precomputeAllocations(ir::Module &module);

    // -------- 函数级生成 --------
    void generateFunction(ir::Function &func); // 生成单个函数的完整汇编
    void resetFunctionState();                 // 重置每函数状态

    // -------- 指令级生成（基于 opcode 分派，无需字符串匹配） --------
    void generateInst(const ir::Instruction &inst); // 分派入口
    void genAlloca(const ir::Instruction &inst);    // alloca → 分配栈空间
    void genStore(const ir::Instruction &inst);     // store  → sw/sb
    void genLoad(const ir::Instruction &inst);      // load   → lw/lb
    void genBinOp(const ir::Instruction &inst); // add/sub/mul/div/rem → 算术指令（含 addi 优化）
    void genICmp(const ir::Instruction &inst);   // icmp   → slt/sub+seqz 等 + 缓存 CmpInfo
    void genCondBr(const ir::Instruction &inst); // br cond → branch fusion 或 bnez
    void genBr(const ir::Instruction &inst);     // br      → j
    void genRet(const ir::Instruction &inst);    // ret     → mv a0 + epilogue + ret
    void genCall(const ir::Instruction &inst);   // call    → 保存/恢复 caller-saved + call

    // -------- 操作数解析 --------
    std::string resolveUse(const ir::Operand &op); // 将 Operand 解析为物理寄存器名（含溢出加载）
    std::string resolveDef(const ir::Operand &op); // 将 def Operand 解析为目标寄存器名
    int getAllocaOffset(int vreg);                 // 查找 alloca vreg 的栈偏移
    int spillSlotToSpOffset(int slot);             // 溢出槽偏移 → sp 正偏移
    void spillDefIfNeeded(const ir::Instruction &inst); // 若 def 被溢出，写回栈

    // -------- 汇编输出 --------
    void emit(const std::string &line); // 输出一条缩进后的汇编指令

    // -------- 栈帧管理 --------
    void emitPrologue();        // （占位，实际由 updateStackFramePlaceholders 回填）
    void emitEpilogue();        // （占位，实际由 updateStackFramePlaceholders 回填）
    void calculateStackFrame(); // 计算栈帧总大小（对齐到 16 字节）
    void updateStackFramePlaceholders(); // 替换 prologue/epilogue 占位符为实际指令
};

#pragma endregion

// 便捷函数：从结构化 IR 模块直接生成 RISC-V 汇编字符串
std::string generateRISCVAssembly(ir::Module &module);

} // namespace toyc
