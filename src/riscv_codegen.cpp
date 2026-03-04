#include "riscv_codegen.h"
#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace toyc {

using namespace ir;

#pragma region 构造与便捷函数

RISCVCodeGen::RISCVCodeGen() {}

// generateRISCVAssembly：便捷入口，封装 RISCVCodeGen 对象的创建和调用
std::string generateRISCVAssembly(Module &module) {
    RISCVCodeGen gen;
    return gen.generate(module);
}

#pragma endregion

#pragma region 主入口与函数级生成

/**
 * @brief 生成整个 IR 模块的 RISC-V 汇编
 * @param module IR 模块
 * @return 完整的汇编文本
 * @details 流程：预计算寄存器分配 → 逐函数生成汇编
 */
std::string RISCVCodeGen::generate(Module &module) {
    output_.clear();
    output_ += "    .text\n";

    precomputeAllocations(module);

    for (auto &func : module.functions)
        generateFunction(*func);

    return output_;
}

// precomputeAllocations：对每个函数进行线性扫描寄存器分配，结果缓存到 funcAllocators_
void RISCVCodeGen::precomputeAllocations(Module &module) {
    funcAllocators_.clear();
    for (auto &func : module.functions) {
        auto allocator = std::make_unique<LinearScanAllocator>(regInfo_);
        allocator->allocate(*func);
        funcAllocators_[func->name] = std::move(allocator);
    }
}

// resetFunctionState：重置每个函数的运行时状态
void RISCVCodeGen::resetFunctionState() {
    allocaOffsets_.clear();
    cmpMap_.clear();
    stackOffset_ = 0;
    totalStackSize_ = 0;
    frameOverhead_ = 0;
    callSaveSize_ = 0;
    callArgAreaSize_ = 0;
    hasReturn_ = false;
}

/**
 * @brief 生成单个函数的完整汇编
 * @details 流程：
 *   1. .globl + 函数标签
 *   2. Prologue 占位符
 *   3. 遍历所有基本块，生成标签和指令
 *   4. 计算栈帧大小，替换 prologue/epilogue 占位符
 *   5. 输出 .size 指令
 */
void RISCVCodeGen::generateFunction(Function &func) {
    resetFunctionState();
    currentFunction_ = func.name;
    isMainFunction_ = (func.name == "main");

    // 预计算帧开销（ra + s0 + callee-saved），供 alloca 偏移使用
    auto &alloc = funcAllocators_[currentFunction_]->getAllocationResult();
    int calleeSavedCount = static_cast<int>(alloc.calleeSavedRegs.size());
    frameOverhead_ = 8 + calleeSavedCount * 4;

    // 预计算函数调用时 caller-saved 保存区大小（基于每个调用点的活跃性取最大值）
    {
        auto &intervals = funcAllocators_[currentFunction_]->getIntervals();
        int maxSaveCount = 0;
        for (auto &bb : func.blocks) {
            for (auto &inst : bb->insts) {
                if (inst->opcode != ir::Opcode::Call)
                    continue;
                int callPosUse = inst->posUse();
                // 确定此调用的 def 物理寄存器（排除不需要保存的 def 寄存器）
                int defPhys = -1;
                if (inst->def.isVReg()) {
                    auto physIt = alloc.vregToPhys.find(inst->def.regId());
                    if (physIt != alloc.vregToPhys.end())
                        defPhys = physIt->second;
                }
                std::set<int> csRegs;
                for (auto &[vreg, physReg] : alloc.vregToPhys) {
                    if (regInfo_.isCallerSaved(physReg) &&
                        !funcAllocators_[currentFunction_]->isSpillTempReg(physReg) &&
                        physReg != defPhys) {
                        auto ivIt = intervals.find(vreg);
                        if (ivIt != intervals.end() && ivIt->second->contains(callPosUse)) {
                            csRegs.insert(physReg);
                        }
                    }
                }
                maxSaveCount = std::max(maxSaveCount, static_cast<int>(csRegs.size()));
            }
        }
        callSaveSize_ = maxSaveCount * 4;
    }

    // 预计算出栈参数区大小（超过 8 个参数的调用需要栈传参）
    {
        int maxStackArgs = 0;
        for (auto &bb : func.blocks) {
            for (auto &inst : bb->insts) {
                if (inst->opcode == ir::Opcode::Call) {
                    int extraArgs = std::max(0, static_cast<int>(inst->ops.size()) - 8);
                    maxStackArgs = std::max(maxStackArgs, extraArgs);
                }
            }
        }
        callArgAreaSize_ = maxStackArgs * 4;
    }

    output_ += "    .globl " + func.name + "\n";
    output_ += func.name + ":\n";

    // Prologue 占位符
    output_ += prologuePlaceholder();
    output_ += '\n';

    // 遍历所有基本块
    for (size_t bi = 0; bi < func.blocks.size(); ++bi) {
        auto &bb = func.blocks[bi];
        if (bi > 0) {
            output_ += makeLabel(bb->name) + ":\n";
        }
        for (auto &inst : bb->insts)
            generateInst(*inst);
    }

    // 计算栈帧并替换占位符
    calculateStackFrame();
    updateStackFramePlaceholders();

    output_ += "    .size " + func.name + ", .-" + func.name + "\n\n";
}

// emit：输出一条带 4 空格缩进的汇编指令
void RISCVCodeGen::emit(const std::string &line) {
    output_ += "    ";
    output_ += line;
    output_ += '\n';
}

// 占位符/标签辅助函数（集中管理格式，消除重复拼接）
std::string RISCVCodeGen::prologuePlaceholder() const {
    return "__PROLOGUE_PLACEHOLDER_" + currentFunction_ + "__";
}
std::string RISCVCodeGen::epiloguePlaceholder() const {
    return "__EPILOGUE_PLACEHOLDER_" + currentFunction_ + "__";
}
std::string RISCVCodeGen::makeLabel(const std::string &name) const {
    return "." + currentFunction_ + "_" + name;
}

#pragma endregion

#pragma region 指令级生成

// generateInst：根据 opcode 分派到对应的生成函数
void RISCVCodeGen::generateInst(const Instruction &inst) {
    switch (inst.opcode) {
    case Opcode::Alloca:
        genAlloca(inst);
        break;
    case Opcode::Store:
        genStore(inst);
        break;
    case Opcode::Load:
        genLoad(inst);
        break;
    case Opcode::Add:
    case Opcode::Sub:
    case Opcode::Mul:
    case Opcode::SDiv:
    case Opcode::SRem:
        genBinOp(inst);
        break;
    case Opcode::ICmp:
        genICmp(inst);
        break;
    case Opcode::CondBr:
        genCondBr(inst);
        break;
    case Opcode::Br:
        genBr(inst);
        break;
    case Opcode::Ret:
        genRet(inst);
        break;
    case Opcode::RetVoid:
        genRet(inst);
        break;
    case Opcode::Call:
        genCall(inst);
        break;
    }
}

// genAlloca：分配局部变量栈空间，记录 vreg → 栈偏移
void RISCVCodeGen::genAlloca(const Instruction &inst) {
    int vreg = inst.defReg();
    int size = (inst.type == "i1") ? 1 : 4;
    stackOffset_ += size;
    // 对齐到 4
    if (stackOffset_ % 4 != 0)
        stackOffset_ += (4 - stackOffset_ % 4);
    allocaOffsets_[vreg] = stackOffset_;
}

// genStore：store 指令 → sw/sb（根据类型选择字节宽度）
void RISCVCodeGen::genStore(const Instruction &inst) {
    // ops[0] = value, ops[1] = ptr (alloca vreg)
    std::string valReg = resolveUse(inst.ops[0]);
    int ptrVreg = inst.ops[1].regId();
    int offset = getAllocaOffset(ptrVreg);

    if (inst.type == "i1") {
        emit("sb " + valReg + ", -" + std::to_string(offset) + "(s0)");
    } else {
        emit("sw " + valReg + ", -" + std::to_string(offset) + "(s0)");
    }
}

// genLoad：load 指令 → lw/lb（含溢出写回）
void RISCVCodeGen::genLoad(const Instruction &inst) {
    // ops[0] = ptr (alloca vreg)
    std::string defReg = resolveDef(inst.def);
    int ptrVreg = inst.ops[0].regId();
    int offset = getAllocaOffset(ptrVreg);

    if (inst.type == "i1") {
        emit("lb " + defReg + ", -" + std::to_string(offset) + "(s0)");
    } else {
        emit("lw " + defReg + ", -" + std::to_string(offset) + "(s0)");
    }

    // 如果 def 被溢出，需要写回栈
    spillDefIfNeeded(inst);
}

/**
 * @brief 算术运算指令生成
 * @details 支持 addi 优化：当 add/sub 的一个操作数为立即数时，
 *          直接生成 addi 而非先 li 再 add
 */
void RISCVCodeGen::genBinOp(const Instruction &inst) {
    std::string defReg = resolveDef(inst.def);

    // addi 优化：add/sub 且立即数在 12 位有符号范围 [-2048, 2047] 内
    auto inAddiRange = [](int v) { return v >= -2048 && v <= 2047; };

    if (inst.opcode == Opcode::Add && inst.ops[1].isImm() && inAddiRange(inst.ops[1].immValue())) {
        std::string lhsReg = resolveUse(inst.ops[0]);
        emit("addi " + defReg + ", " + lhsReg + ", " + std::to_string(inst.ops[1].immValue()));
        spillDefIfNeeded(inst);
        return;
    }
    if (inst.opcode == Opcode::Add && inst.ops[0].isImm() && inAddiRange(inst.ops[0].immValue())) {
        std::string rhsReg = resolveUse(inst.ops[1]);
        emit("addi " + defReg + ", " + rhsReg + ", " + std::to_string(inst.ops[0].immValue()));
        spillDefIfNeeded(inst);
        return;
    }
    if (inst.opcode == Opcode::Sub && inst.ops[1].isImm() && inAddiRange(-inst.ops[1].immValue())) {
        std::string lhsReg = resolveUse(inst.ops[0]);
        emit("addi " + defReg + ", " + lhsReg + ", " + std::to_string(-inst.ops[1].immValue()));
        spillDefIfNeeded(inst);
        return;
    }

    std::string lhsReg = resolveUse(inst.ops[0]);
    std::string rhsReg = resolveUse(inst.ops[1]);

    std::string op;
    switch (inst.opcode) {
    case Opcode::Add:
        op = "add";
        break;
    case Opcode::Sub:
        op = "sub";
        break;
    case Opcode::Mul:
        op = "mul";
        break;
    case Opcode::SDiv:
        op = "div";
        break;
    case Opcode::SRem:
        op = "rem";
        break;
    default:
        return;
    }

    emit(op + " " + defReg + ", " + lhsReg + ", " + rhsReg);
    spillDefIfNeeded(inst);
}

/**
 * @brief 比较指令生成
 * @details 生成兜底指令（slt/sub+seqz 等），同时将比较信息缓存到 cmpMap_，
 *          供后续 genCondBr 进行 branch fusion。
 *
 * 立即数优化：
 *   - 与 0 比较：使用 seqz/snez/slti/slt+zero 避免 li 临时寄存器
 *   - SLT/SGE + 12 位立即数：使用 slti 避免 li 临时寄存器
 *   - SLE/SGT + 12 位立即数：改写为 slti (imm+1) 避免 li 临时寄存器
 */
void RISCVCodeGen::genICmp(const Instruction &inst) {
    auto inSltiRange = [](int v) { return v >= -2048 && v <= 2047; };

    // ---- 优化路径 1：与立即数 0 比较 ----
    // 可利用 zero 寄存器 (x0)，省去 li 指令和临时寄存器
    bool lhsIsZero = inst.ops[0].isImm() && inst.ops[0].immValue() == 0;
    bool rhsIsZero = inst.ops[1].isImm() && inst.ops[1].immValue() == 0;

    if (lhsIsZero || rhsIsZero) {
        // 解析非零一侧的操作数
        const Operand &nonZeroOp = rhsIsZero ? inst.ops[0] : inst.ops[1];
        std::string reg = resolveUse(nonZeroOp);
        std::string defReg = resolveDef(inst.def);

        // 若 0 在左侧 (icmp pred 0, x)，交换为等价的 (icmp swappedPred x, 0)
        CmpPred effPred = inst.cmpPred;
        if (lhsIsZero && !rhsIsZero) {
            switch (inst.cmpPred) {
            case CmpPred::EQ:
                effPred = CmpPred::EQ;
                break;
            case CmpPred::NE:
                effPred = CmpPred::NE;
                break;
            case CmpPred::SLT:
                effPred = CmpPred::SGT;
                break; // 0 < x → x > 0
            case CmpPred::SGT:
                effPred = CmpPred::SLT;
                break; // 0 > x → x < 0
            case CmpPred::SLE:
                effPred = CmpPred::SGE;
                break; // 0 <= x → x >= 0
            case CmpPred::SGE:
                effPred = CmpPred::SLE;
                break; // 0 >= x → x <= 0
            }
        }

        // branch fusion 缓存：使用 zero 寄存器名，genCondBr 可生成 beq/bne/blt/... reg, zero
        cmpMap_[inst.defReg()] = CmpInfo{effPred, reg, kRegZero};

        // 兜底指令
        switch (effPred) {
        case CmpPred::EQ:
            emit("seqz " + defReg + ", " + reg);
            break;
        case CmpPred::NE:
            emit("snez " + defReg + ", " + reg);
            break;
        case CmpPred::SLT:
            emit("slti " + defReg + ", " + reg + ", 0");
            break;
        case CmpPred::SGT:
            emit("slt " + defReg + ", zero, " + reg);
            break;
        case CmpPred::SLE:
            emit("slt " + defReg + ", zero, " + reg);
            emit("xori " + defReg + ", " + defReg + ", 1");
            break;
        case CmpPred::SGE:
            emit("slti " + defReg + ", " + reg + ", 0");
            emit("xori " + defReg + ", " + defReg + ", 1");
            break;
        }
        spillDefIfNeeded(inst);
        return;
    }

    // ---- 优化路径 2：SLT/SGE + RHS 为 12 位有符号立即数 → slti ----
    if ((inst.cmpPred == CmpPred::SLT || inst.cmpPred == CmpPred::SGE) && inst.ops[1].isImm() &&
        inSltiRange(inst.ops[1].immValue())) {
        std::string lhsReg = resolveUse(inst.ops[0]);
        std::string defReg = resolveDef(inst.def);
        std::string immStr = std::to_string(inst.ops[1].immValue());
        // 不缓存到 cmpMap_（分支指令不支持立即数操作数），condBr 使用 bnez 回退
        if (inst.cmpPred == CmpPred::SLT) {
            emit("slti " + defReg + ", " + lhsReg + ", " + immStr);
        } else { // SGE = NOT(SLT)
            emit("slti " + defReg + ", " + lhsReg + ", " + immStr);
            emit("xori " + defReg + ", " + defReg + ", 1");
        }
        spillDefIfNeeded(inst);
        return;
    }

    // ---- 优化路径 3：SLE/SGT + RHS 为立即数且 imm+1 在 slti 范围 ----
    // 整数恒等式：x <= N ↔ x < N+1，x > N ↔ NOT(x < N+1)
    if ((inst.cmpPred == CmpPred::SLE || inst.cmpPred == CmpPred::SGT) && inst.ops[1].isImm() &&
        inSltiRange(inst.ops[1].immValue() + 1)) {
        std::string lhsReg = resolveUse(inst.ops[0]);
        std::string defReg = resolveDef(inst.def);
        std::string immStr = std::to_string(inst.ops[1].immValue() + 1);
        if (inst.cmpPred == CmpPred::SLE) {
            emit("slti " + defReg + ", " + lhsReg + ", " + immStr);
        } else { // SGT = NOT(SLE) = NOT(SLT(imm+1))
            emit("slti " + defReg + ", " + lhsReg + ", " + immStr);
            emit("xori " + defReg + ", " + defReg + ", 1");
        }
        spillDefIfNeeded(inst);
        return;
    }

    // ---- 默认路径：两个操作数均解析为寄存器 ----
    std::string lhsReg = resolveUse(inst.ops[0]);
    std::string rhsReg = resolveUse(inst.ops[1]);
    std::string defReg = resolveDef(inst.def);

    // 缓存比较信息供 branch fusion
    cmpMap_[inst.defReg()] = CmpInfo{inst.cmpPred, lhsReg, rhsReg};

    // 兜底指令（供值使用场景）
    switch (inst.cmpPred) {
    case CmpPred::EQ:
        emit("sub " + defReg + ", " + lhsReg + ", " + rhsReg);
        emit("seqz " + defReg + ", " + defReg);
        break;
    case CmpPred::NE:
        emit("sub " + defReg + ", " + lhsReg + ", " + rhsReg);
        emit("snez " + defReg + ", " + defReg);
        break;
    case CmpPred::SLT:
        emit("slt " + defReg + ", " + lhsReg + ", " + rhsReg);
        break;
    case CmpPred::SGT:
        emit("slt " + defReg + ", " + rhsReg + ", " + lhsReg);
        break;
    case CmpPred::SLE:
        emit("slt " + defReg + ", " + rhsReg + ", " + lhsReg);
        emit("xori " + defReg + ", " + defReg + ", 1");
        break;
    case CmpPred::SGE:
        emit("slt " + defReg + ", " + lhsReg + ", " + rhsReg);
        emit("xori " + defReg + ", " + defReg + ", 1");
        break;
    }
    spillDefIfNeeded(inst);
}

/**
 * @brief 条件分支指令生成
 * @details 尝试 branch fusion：如果条件 vreg 在 cmpMap_ 中有缓存，
 *          直接生成 beq/bne/blt/bgt/ble/bge；否则回退到 bnez + j
 */
void RISCVCodeGen::genCondBr(const Instruction &inst) {
    // ops[0] = cond, ops[1] = true label, ops[2] = false label
    std::string trueLabel = makeLabel(inst.ops[1].labelName());
    std::string falseLabel = makeLabel(inst.ops[2].labelName());

    int condVreg = inst.ops[0].isVReg() ? inst.ops[0].regId() : -1;
    auto cmpIt = cmpMap_.find(condVreg);

    if (cmpIt != cmpMap_.end()) {
        // Branch fusion
        auto &cmp = cmpIt->second;
        std::string brOp;
        switch (cmp.pred) {
        case CmpPred::EQ:
            brOp = "beq";
            break;
        case CmpPred::NE:
            brOp = "bne";
            break;
        case CmpPred::SLT:
            brOp = "blt";
            break;
        case CmpPred::SGT:
            brOp = "bgt";
            break;
        case CmpPred::SLE:
            brOp = "ble";
            break;
        case CmpPred::SGE:
            brOp = "bge";
            break;
        }
        emit(brOp + " " + cmp.lhsReg + ", " + cmp.rhsReg + ", " + trueLabel);
        emit("j " + falseLabel);
        cmpMap_.erase(cmpIt);
    } else {
        std::string condReg = resolveUse(inst.ops[0]);
        emit("bnez " + condReg + ", " + trueLabel);
        emit("j " + falseLabel);
    }
}

// genBr：无条件跳转 → j
void RISCVCodeGen::genBr(const Instruction &inst) {
    std::string target = makeLabel(inst.ops[0].labelName());
    emit("j " + target);
}

// genRet：返回指令 → mv a0 + epilogue 占位符 + ret
void RISCVCodeGen::genRet(const Instruction &inst) {
    hasReturn_ = true;

    if (inst.opcode == Opcode::Ret && !inst.ops.empty()) {
        std::string valReg = resolveUse(inst.ops[0]);
        if (valReg != kRegA0)
            emit("mv a0, " + valReg);
    }

    // Epilogue 占位符
    output_ += epiloguePlaceholder();
    output_ += '\n';
    emit("ret");
}

/**
 * @brief 函数调用指令生成
 * @details 流程：
 *   1. 仅保存在调用点活跃的调用者保存寄存器到栈
 *   2. 移动参数到 a0-a7
 *   3. 发出 call 指令
 *   4. 恢复调用者保存寄存器
 *   5. 将返回值从 a0 移到目标寄存器
 */
void RISCVCodeGen::genCall(const Instruction &inst) {
    auto &alloc = funcAllocators_[currentFunction_]->getAllocationResult();
    auto &intervals = funcAllocators_[currentFunction_]->getIntervals();
    int callPosUse = inst.posUse();

    // 确定 def 对应的物理寄存器（用于跳过保存/恢复）
    int defPhysReg = -1;
    if (inst.def.isVReg()) {
        auto physIt = alloc.vregToPhys.find(inst.def.regId());
        if (physIt != alloc.vregToPhys.end())
            defPhysReg = physIt->second;
    }

    // 收集需要保存的调用者保存寄存器（仅保存在调用点活跃的，排除溢出临时寄存器和 def 寄存器）
    std::vector<int> savedRegs;
    for (auto &[vreg, physReg] : alloc.vregToPhys) {
        if (regInfo_.isCallerSaved(physReg) &&
            !funcAllocators_[currentFunction_]->isSpillTempReg(physReg) && physReg != defPhysReg) {
            // 检查 vreg 在调用点是否活跃
            auto ivIt = intervals.find(vreg);
            if (ivIt != intervals.end() && ivIt->second->contains(callPosUse)) {
                if (std::find(savedRegs.begin(), savedRegs.end(), physReg) == savedRegs.end())
                    savedRegs.push_back(physReg);
            }
        }
    }
    std::sort(savedRegs.begin(), savedRegs.end());

    // 保存到栈（使用 sp 相对偏移，位于出栈参数区之上）
    std::map<int, int> regToSaveOffset; // 物理寄存器 → 保存偏移
    int saveOffset = callArgAreaSize_;
    for (int reg : savedRegs) {
        emit("sw " + regInfo_.getRegName(reg) + ", " + std::to_string(saveOffset) + "(sp)");
        regToSaveOffset[reg] = saveOffset;
        saveOffset += 4;
    }

    // 将超过 8 个的参数存放到出栈参数区 sp+0, sp+4, ...
    for (size_t i = 8; i < inst.ops.size(); ++i) {
        int argOffset = static_cast<int>(i - 8) * 4;
        const auto &op = inst.ops[i];
        if (op.isImm()) {
            auto &allocator = funcAllocators_[currentFunction_];
            int tmpReg = allocator->allocateSpillTempReg();
            std::string tmpName = regInfo_.getRegName(tmpReg);
            emit("li " + tmpName + ", " + std::to_string(op.immValue()));
            emit("sw " + tmpName + ", " + std::to_string(argOffset) + "(sp)");
        } else if (op.isVReg()) {
            int vreg = op.regId();
            auto physIt = alloc.vregToPhys.find(vreg);
            if (physIt != alloc.vregToPhys.end()) {
                int physReg = physIt->second;
                auto saveIt = regToSaveOffset.find(physReg);
                if (saveIt != regToSaveOffset.end()) {
                    auto &allocator = funcAllocators_[currentFunction_];
                    int tmpReg = allocator->allocateSpillTempReg();
                    std::string tmpName = regInfo_.getRegName(tmpReg);
                    emit("lw " + tmpName + ", " + std::to_string(saveIt->second) + "(sp)");
                    emit("sw " + tmpName + ", " + std::to_string(argOffset) + "(sp)");
                } else {
                    std::string srcReg = regInfo_.getRegName(physReg);
                    emit("sw " + srcReg + ", " + std::to_string(argOffset) + "(sp)");
                }
            } else {
                auto stackIt = alloc.vregToStack.find(vreg);
                if (stackIt != alloc.vregToStack.end()) {
                    auto &allocator = funcAllocators_[currentFunction_];
                    int tmpReg = allocator->allocateSpillTempReg();
                    std::string tmpName = regInfo_.getRegName(tmpReg);
                    if (stackIt->second > 0) {
                        emit("lw " + tmpName + ", " + std::to_string(stackIt->second - 4) + "(s0)");
                    } else {
                        int spOffset = spillSlotToSpOffset(stackIt->second);
                        emit("lw " + tmpName + ", " + std::to_string(spOffset) + "(sp)");
                    }
                    emit("sw " + tmpName + ", " + std::to_string(argOffset) + "(sp)");
                }
            }
        }
    }

    // 移动参数到 a0-a7
    // 直接从已保存的栈位置/溢出槽加载到目标寄存器，彻底避免并行移动冲突
    for (size_t i = 0; i < inst.ops.size() && i < 8; ++i) {
        std::string target = "a" + std::to_string(i);
        const auto &op = inst.ops[i];

        if (op.isImm()) {
            emit("li " + target + ", " + std::to_string(op.immValue()));
        } else if (op.isBoolLit()) {
            emit("li " + target + ", " + std::to_string(op.boolValue() ? 1 : 0));
        } else if (op.isVReg()) {
            int vreg = op.regId();
            // 分配到物理寄存器的 vreg
            auto physIt = alloc.vregToPhys.find(vreg);
            if (physIt != alloc.vregToPhys.end()) {
                int physReg = physIt->second;
                auto saveIt = regToSaveOffset.find(physReg);
                if (saveIt != regToSaveOffset.end()) {
                    // caller-saved：从已保存的栈位置加载（避免并行移动冲突）
                    emit("lw " + target + ", " + std::to_string(saveIt->second) + "(sp)");
                } else {
                    // callee-saved：不会被覆盖，直接 mv
                    std::string srcReg = regInfo_.getRegName(physReg);
                    if (srcReg != target)
                        emit("mv " + target + ", " + srcReg);
                }
            } else {
                // 溢出到栈的 vreg：从溢出槽直接加载
                auto stackIt = alloc.vregToStack.find(vreg);
                if (stackIt != alloc.vregToStack.end()) {
                    int spOffset = spillSlotToSpOffset(stackIt->second);
                    emit("lw " + target + ", " + std::to_string(spOffset) + "(sp)");
                }
            }
        }
    }

    // 调用
    emit("call " + inst.callee);

    // 结果从 a0 移到目标寄存器（在恢复 caller-saved 之前，防止 a0 被覆盖）
    std::string defReg = resolveDef(inst.def);
    if (defReg != kRegA0)
        emit("mv " + defReg + ", a0");

    // 恢复 caller-saved 寄存器
    saveOffset = callArgAreaSize_;
    for (int reg : savedRegs) {
        emit("lw " + regInfo_.getRegName(reg) + ", " + std::to_string(saveOffset) + "(sp)");
        saveOffset += 4;
    }

    spillDefIfNeeded(inst);
}

#pragma endregion

#pragma region 操作数解析

/**
 * @brief 将 use 操作数解析为物理寄存器名
 * @details 立即数/布尔值 → li 加载到临时寄存器；
 *          虚拟寄存器 → 查找分配结果，溢出时从栈加载到临时寄存器
 */
std::string RISCVCodeGen::resolveUse(const Operand &op) {
    if (op.isImm()) {
        auto &allocator = funcAllocators_[currentFunction_];
        int tmpReg = allocator->allocateSpillTempReg();
        std::string tmpName = regInfo_.getRegName(tmpReg);
        emit("li " + tmpName + ", " + std::to_string(op.immValue()));
        return tmpName;
    }
    if (op.isBoolLit()) {
        auto &allocator = funcAllocators_[currentFunction_];
        int tmpReg = allocator->allocateSpillTempReg();
        std::string tmpName = regInfo_.getRegName(tmpReg);
        emit("li " + tmpName + ", " + std::to_string(op.boolValue() ? 1 : 0));
        return tmpName;
    }
    if (op.isVReg()) {
        int vreg = op.regId();
        auto &alloc = funcAllocators_[currentFunction_]->getAllocationResult();

        // 物理寄存器
        auto physIt = alloc.vregToPhys.find(vreg);
        if (physIt != alloc.vregToPhys.end())
            return regInfo_.getRegName(physIt->second);

        // 溢出到栈或栈传入的参数
        auto stackIt = alloc.vregToStack.find(vreg);
        if (stackIt != alloc.vregToStack.end()) {
            auto &allocator = funcAllocators_[currentFunction_];
            int tmpReg = allocator->allocateSpillTempReg();
            std::string tmpName = regInfo_.getRegName(tmpReg);
            if (stackIt->second > 0) {
                // 正偏移 = 栈传入参数：位于调用者帧底部，即 s0 + (slot-4)
                int s0Offset = stackIt->second - 4;
                emit("lw " + tmpName + ", " + std::to_string(s0Offset) + "(s0)");
            } else {
                // 负偏移 = 溢出槽
                int spOffset = spillSlotToSpOffset(stackIt->second);
                emit("lw " + tmpName + ", " + std::to_string(spOffset) + "(sp)");
            }
            return tmpName;
        }

        return kRegA0;
    }
    return kRegZero;
}

// resolveDef：将 def 操作数解析为目标物理寄存器名（溢出时返回临时寄存器）
std::string RISCVCodeGen::resolveDef(const Operand &op) {
    if (!op.isVReg()) {
        lastDefRegName_ = kRegA0;
        return lastDefRegName_;
    }

    int vreg = op.regId();
    auto &alloc = funcAllocators_[currentFunction_]->getAllocationResult();

    auto physIt = alloc.vregToPhys.find(vreg);
    if (physIt != alloc.vregToPhys.end()) {
        lastDefRegName_ = regInfo_.getRegName(physIt->second);
        return lastDefRegName_;
    }

    // 溢出 — 返回临时寄存器
    auto &allocator = funcAllocators_[currentFunction_];
    int tmpReg = allocator->allocateSpillTempReg();
    lastDefRegName_ = regInfo_.getRegName(tmpReg);
    return lastDefRegName_;
}

// getAllocaOffset：查找 alloca vreg 对应的栈偏移（含 frameOverhead_ 以越过 ra/s0/callee-saved
// 区域）
int RISCVCodeGen::getAllocaOffset(int vreg) {
    auto it = allocaOffsets_.find(vreg);
    return (it != allocaOffsets_.end()) ? it->second + frameOverhead_ : 0;
}

// spillSlotToSpOffset：将分配器的溢出槽偏移（负值，如 -4, -8）转换为 sp 正偏移
// 帧底部布局：[0, argArea) = 出栈参数 | [argArea, argArea+callSave) = caller-saved
//             | [argArea+callSave, argArea+callSave+spillSize) = 溢出
int RISCVCodeGen::spillSlotToSpOffset(int slot) {
    return callArgAreaSize_ + callSaveSize_ + ((-slot) - 4);
}

// spillDefIfNeeded：若 def 被溢出，将临时寄存器写回栈槽
void RISCVCodeGen::spillDefIfNeeded(const Instruction &inst) {
    int dr = inst.defReg();
    if (dr < 0)
        return;
    auto &alloc = funcAllocators_[currentFunction_]->getAllocationResult();
    auto it = alloc.vregToStack.find(dr);
    if (it != alloc.vregToStack.end() && it->second < 0 &&
        allocaOffsets_.find(dr) == allocaOffsets_.end()) {
        // 使用 resolveDef 保存的同一寄存器名（不依赖 counter 状态）
        int spOffset = spillSlotToSpOffset(it->second);
        emit("sw " + lastDefRegName_ + ", " + std::to_string(spOffset) + "(sp)");
    }
}

#pragma endregion

#pragma region 栈帧管理

void RISCVCodeGen::emitPrologue() {}
void RISCVCodeGen::emitEpilogue() {}

/**
 * @brief 计算函数栈帧总大小
 * @details 组成：局部变量空间 + ra/s0 保存空间 + callee-saved 寄存器 + 溢出栈槽
 * @note 最终对齐到 16 字节边界
 */
void RISCVCodeGen::calculateStackFrame() {
    auto &alloc = funcAllocators_[currentFunction_]->getAllocationResult();

    int allocaSize = stackOffset_;
    int calleeSavedCount = static_cast<int>(alloc.calleeSavedRegs.size());
    int spillSize = 0;
    for (auto &[vreg, slot] : alloc.vregToStack) {
        if (slot < 0) { // 仅计算溢出槽（负偏移），不计入传入栈参数（正偏移）
            int absSlot = -slot;
            spillSize = std::max(spillSize, absSlot);
        }
    }

    // ra + s0 = 8 字节
    int frameOverhead = 8 + calleeSavedCount * 4;
    totalStackSize_ = allocaSize + frameOverhead + spillSize + callSaveSize_ + callArgAreaSize_;

    // 对齐到 16
    totalStackSize_ = (totalStackSize_ + 15) & ~15;
}

/**
 * @brief 替换 prologue/epilogue 占位符为实际的栈帧指令
 * @details prologue: addi sp → sw ra/s0 → sw callee-saved → addi s0
 *          epilogue: lw callee-saved → lw ra/s0 → addi sp
 */
void RISCVCodeGen::updateStackFramePlaceholders() {
    auto &alloc = funcAllocators_[currentFunction_]->getAllocationResult();

    // 生成 prologue
    std::string prologue;
    prologue += "    addi sp, sp, -" + std::to_string(totalStackSize_) + "\n";
    prologue += "    sw ra, " + std::to_string(totalStackSize_ - 4) + "(sp)\n";
    prologue += "    sw s0, " + std::to_string(totalStackSize_ - 8) + "(sp)\n";
    prologue += "    addi s0, sp, " + std::to_string(totalStackSize_) + "\n";

    int offset = totalStackSize_ - 12;
    for (int reg : alloc.calleeSavedRegs) {
        prologue += "    sw " + regInfo_.getRegName(reg) + ", " + std::to_string(offset) + "(sp)\n";
        offset -= 4;
    }

    std::string prologueTag = prologuePlaceholder();
    size_t pos = output_.find(prologueTag);
    if (pos != std::string::npos) {
        output_.replace(pos, prologueTag.size() + 1, prologue);
    }

    // 生成 epilogue
    std::string epilogue;
    offset = totalStackSize_ - 12;
    for (int reg : alloc.calleeSavedRegs) {
        epilogue += "    lw " + regInfo_.getRegName(reg) + ", " + std::to_string(offset) + "(sp)\n";
        offset -= 4;
    }
    epilogue += "    lw ra, " + std::to_string(totalStackSize_ - 4) + "(sp)\n";
    epilogue += "    lw s0, " + std::to_string(totalStackSize_ - 8) + "(sp)\n";
    epilogue += "    addi sp, sp, " + std::to_string(totalStackSize_) + "\n";

    std::string epilogueTag = epiloguePlaceholder();
    while (true) {
        pos = output_.find(epilogueTag);
        if (pos == std::string::npos)
            break;
        output_.replace(pos, epilogueTag.size() + 1, epilogue);
    }
}

#pragma endregion

} // namespace toyc
