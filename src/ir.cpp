#include "ir.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace toyc {
namespace ir {

#pragma region Opcode 工具函数

// opcodeToString：将 Opcode 枚举值转换为对应 LLVM IR 关键字
std::string opcodeToString(Opcode op) {
    switch (op) {
    case Opcode::Alloca:
        return "alloca";
    case Opcode::Load:
        return "load";
    case Opcode::Store:
        return "store";
    case Opcode::Add:
        return "add";
    case Opcode::Sub:
        return "sub";
    case Opcode::Mul:
        return "mul";
    case Opcode::SDiv:
        return "sdiv";
    case Opcode::SRem:
        return "srem";
    case Opcode::ICmp:
        return "icmp";
    case Opcode::Br:
        return "br";
    case Opcode::CondBr:
        return "br"; // 条件分支和无条件分支在 LLVM IR 文本中都是 "br"
    case Opcode::Ret:
        return "ret";
    case Opcode::RetVoid:
        return "ret";
    case Opcode::Call:
        return "call";
    }
    return "unknown";
}

// stringToArithOpcode：从字符串解析算术运算 Opcode
Opcode stringToArithOpcode(const std::string &s) {
    if (s == "add")
        return Opcode::Add;
    if (s == "sub")
        return Opcode::Sub;
    if (s == "mul")
        return Opcode::Mul;
    if (s == "sdiv")
        return Opcode::SDiv;
    if (s == "srem")
        return Opcode::SRem;
    throw std::runtime_error("Unknown arithmetic opcode: " + s);
}

#pragma endregion

#pragma region 比较谓词 CmpPred

// cmpPredToString：将比较谓词转换为 LLVM IR 文本
std::string cmpPredToString(CmpPred pred) {
    switch (pred) {
    case CmpPred::EQ:
        return "eq";
    case CmpPred::NE:
        return "ne";
    case CmpPred::SLT:
        return "slt";
    case CmpPred::SGT:
        return "sgt";
    case CmpPred::SLE:
        return "sle";
    case CmpPred::SGE:
        return "sge";
    }
    return "eq";
}

// stringToCmpPred：从 LLVM IR 文本解析比较谓词
CmpPred stringToCmpPred(const std::string &s) {
    if (s == "eq")
        return CmpPred::EQ;
    if (s == "ne")
        return CmpPred::NE;
    if (s == "slt")
        return CmpPred::SLT;
    if (s == "sgt")
        return CmpPred::SGT;
    if (s == "sle")
        return CmpPred::SLE;
    if (s == "sge")
        return CmpPred::SGE;
    return CmpPred::EQ; // 默认返回 EQ
}

#pragma endregion

#pragma region 操作数 Operand

// toString：将操作数序列化为 LLVM IR 文本
// VReg → "%N"，Imm → "N"，Label → "%name"，BoolLit → "true"/"false"
std::string Operand::toString() const {
    switch (kind_) {
    case Kind::None:
        return "";
    case Kind::VReg:
        return "%" + std::to_string(value_);
    case Kind::Imm:
        return std::to_string(value_);
    case Kind::Label:
        return "%" + label_;
    case Kind::BoolLit:
        return value_ ? "true" : "false";
    }
    return "";
}

#pragma endregion

#pragma region 指令工厂方法

// makeAlloca：创建栈分配指令 %def = alloca type, align N
Instruction Instruction::makeAlloca(Operand def, const std::string &type, int align) {
    Instruction i;
    i.opcode = Opcode::Alloca;
    i.def = def;
    i.type = type;
    i.align = align;
    return i;
}

// makeLoad：创建加载指令 %def = load type, ptr %ptr, align N
Instruction Instruction::makeLoad(Operand def, const std::string &type, Operand ptr, int align) {
    Instruction i;
    i.opcode = Opcode::Load;
    i.def = def;
    i.type = type;
    i.ops = {ptr};
    i.align = align;
    return i;
}

// makeStore：创建存储指令 store type value, ptr %ptr, align N
Instruction Instruction::makeStore(const std::string &type, Operand value, Operand ptr, int align) {
    Instruction i;
    i.opcode = Opcode::Store;
    i.type = type;
    i.ops = {value, ptr};
    i.align = align;
    return i;
}

// makeBinOp：创建二元运算指令 %def = op nsw type lhs, rhs
Instruction Instruction::makeBinOp(Opcode op, Operand def, const std::string &type, Operand lhs,
                                   Operand rhs) {
    Instruction i;
    i.opcode = op;
    i.def = def;
    i.type = type;
    i.ops = {lhs, rhs};
    i.nsw = true;
    return i;
}

// makeICmp：创建整数比较指令 %def = icmp pred type lhs, rhs
Instruction Instruction::makeICmp(CmpPred pred, Operand def, const std::string &type, Operand lhs,
                                  Operand rhs) {
    Instruction i;
    i.opcode = Opcode::ICmp;
    i.def = def;
    i.type = type;
    i.ops = {lhs, rhs};
    i.cmpPred = pred;
    return i;
}

// makeBr：创建无条件跳转指令 br label %target
Instruction Instruction::makeBr(Operand target) {
    Instruction i;
    i.opcode = Opcode::Br;
    i.ops = {target};
    return i;
}

// makeCondBr：创建条件分支指令 br i1 %cond, label %true, label %false
Instruction Instruction::makeCondBr(Operand cond, Operand trueTarget, Operand falseTarget) {
    Instruction i;
    i.opcode = Opcode::CondBr;
    i.ops = {cond, trueTarget, falseTarget};
    return i;
}

// makeRet：创建带返回值的返回指令 ret type value
Instruction Instruction::makeRet(const std::string &type, Operand value) {
    Instruction i;
    i.opcode = Opcode::Ret;
    i.type = type;
    i.ops = {value};
    return i;
}

// makeRetVoid：创建无返回值的返回指令 ret void
Instruction Instruction::makeRetVoid() {
    Instruction i;
    i.opcode = Opcode::RetVoid;
    i.type = "void";
    return i;
}

// makeCall：创建函数调用指令 %def = call retType @callee(args...)
Instruction Instruction::makeCall(Operand def, const std::string &retType,
                                  const std::string &callee, std::vector<Operand> args) {
    Instruction i;
    i.opcode = Opcode::Call;
    i.def = def;
    i.type = retType;
    i.callee = callee;
    i.ops = std::move(args);
    return i;
}

#pragma endregion

#pragma region 指令查询方法

// defReg：返回指令定义（写入）的虚拟寄存器 ID，若无定义返回 -1
int Instruction::defReg() const { return def.isVReg() ? def.regId() : -1; }

// useRegs：返回指令使用（读取）的所有虚拟寄存器 ID 列表
// 不同 opcode 的使用寄存器位置不同，需逐类型处理
std::vector<int> Instruction::useRegs() const {
    std::vector<int> result;
    switch (opcode) {
    case Opcode::Alloca:
        // alloca 无 use
        break;
    case Opcode::Load:
        // ops[0] = ptr (VReg)
        if (ops.size() > 0 && ops[0].isVReg())
            result.push_back(ops[0].regId());
        break;
    case Opcode::Store:
        // ops[0] = value, ops[1] = ptr，两个操作数都可能是寄存器
        for (auto &op : ops)
            if (op.isVReg())
                result.push_back(op.regId());
        break;
    case Opcode::Add:
    case Opcode::Sub:
    case Opcode::Mul:
    case Opcode::SDiv:
    case Opcode::SRem:
        // 算术运算：ops[0] = lhs, ops[1] = rhs
        for (auto &op : ops)
            if (op.isVReg())
                result.push_back(op.regId());
        break;
    case Opcode::ICmp:
        for (auto &op : ops)
            if (op.isVReg())
                result.push_back(op.regId());
        break;
    case Opcode::CondBr:
        // 条件分支：ops[0] = 条件寄存器，ops[1,2] = 目标标签
        if (ops.size() > 0 && ops[0].isVReg())
            result.push_back(ops[0].regId());
        break;
    case Opcode::Br:
        // 无条件跳转无寄存器使用
        break;
    case Opcode::Ret:
        if (ops.size() > 0 && ops[0].isVReg())
            result.push_back(ops[0].regId());
        break;
    case Opcode::RetVoid:
        break;
    case Opcode::Call:
        // 函数调用：ops 中所有操作数为实参
        for (auto &op : ops)
            if (op.isVReg())
                result.push_back(op.regId());
        break;
    }
    return result;
}

// isTerminator：判断是否为终结指令（br/condbr/ret/retvoid）
bool Instruction::isTerminator() const {
    return opcode == Opcode::Br || opcode == Opcode::CondBr || opcode == Opcode::Ret ||
           opcode == Opcode::RetVoid;
}

// isCallInst：判断是否为函数调用指令
bool Instruction::isCallInst() const { return opcode == Opcode::Call; }

// branchTargets：返回分支目标基本块标签列表
// Br 返回一个目标，CondBr 返回两个目标（true 和 false 分支）
std::vector<std::string> Instruction::branchTargets() const {
    std::vector<std::string> targets;
    switch (opcode) {
    case Opcode::Br:
        if (ops.size() > 0 && ops[0].isLabel())
            targets.push_back(ops[0].labelName());
        break;
    case Opcode::CondBr:
        if (ops.size() > 1 && ops[1].isLabel())
            targets.push_back(ops[1].labelName());
        if (ops.size() > 2 && ops[2].isLabel())
            targets.push_back(ops[2].labelName());
        break;
    default:
        break;
    }
    return targets;
}

// branchCondReg：返回条件分支的条件寄存器 ID，非条件分支返回 -1
int Instruction::branchCondReg() const {
    if (opcode == Opcode::CondBr && ops.size() > 0 && ops[0].isVReg())
        return ops[0].regId();
    return -1;
}

// toString：将指令序列化为 LLVM IR 文本行
std::string Instruction::toString() const {
    std::string s;
    switch (opcode) {
    case Opcode::Alloca:
        s = def.toString() + " = alloca " + type + ", align " + std::to_string(align);
        break;
    case Opcode::Load:
        s = def.toString() + " = load " + type + ", ptr " + ops[0].toString() + ", align " +
            std::to_string(align);
        break;
    case Opcode::Store: {
        std::string valStr = ops[0].toString();
        s = "store " + type + " " + valStr + ", ptr " + ops[1].toString() + ", align " +
            std::to_string(align);
        break;
    }
    case Opcode::Add:
    case Opcode::Sub:
    case Opcode::Mul:
    case Opcode::SDiv:
    case Opcode::SRem:
        s = def.toString() + " = " + opcodeToString(opcode) + (nsw ? " nsw" : "") + " " + type +
            " " + ops[0].toString() + ", " + ops[1].toString();
        break;
    case Opcode::ICmp:
        s = def.toString() + " = icmp " + cmpPredToString(cmpPred) + " " + type + " " +
            ops[0].toString() + ", " + ops[1].toString();
        break;
    case Opcode::Br:
        s = "br label " + ops[0].toString();
        break;
    case Opcode::CondBr:
        s = "br i1 " + ops[0].toString() + ", label " + ops[1].toString() + ", label " +
            ops[2].toString();
        break;
    case Opcode::Ret:
        s = "ret " + type + " " + ops[0].toString();
        break;
    case Opcode::RetVoid:
        s = "ret void";
        break;
    case Opcode::Call: {
        s = def.toString() + " = call " + type + " @" + callee + "(";
        for (size_t j = 0; j < ops.size(); ++j) {
            if (j > 0)
                s += ", ";
            s += "i32 noundef " + ops[j].toString();
        }
        s += ")";
        break;
    }
    }
    return s;
}

#pragma endregion

#pragma region 基本块 BasicBlock

// firstPos：返回块内第一条指令的定义位置（用于活跃区间计算）
int BasicBlock::firstPos() const { return insts.empty() ? -1 : insts.front()->posDef(); }

// lastPos：返回块内最后一条指令的使用位置
int BasicBlock::lastPos() const { return insts.empty() ? -1 : insts.back()->posUse(); }

#pragma endregion

#pragma region 函数 Function

// entryBlock：返回函数入口基本块（即第一个基本块）
BasicBlock *Function::entryBlock() const { return blocks.empty() ? nullptr : blocks[0].get(); }

// buildCFG：根据分支指令构建控制流图（计算每个基本块的 succs 和 preds）
void Function::buildCFG() {
    for (auto &block : blocks) {
        block->succs.clear();
        block->preds.clear();
    }
    for (auto &block : blocks) {
        if (block->insts.empty())
            continue;
        auto &lastInst = block->insts.back();
        if (lastInst->isTerminator()) {
            for (const auto &target : lastInst->branchTargets()) {
                auto it = blockMap.find(target);
                if (it != blockMap.end()) {
                    block->succs.push_back(it->second);
                    it->second->preds.push_back(block.get());
                }
            }
        } else {
            // 无终结指令时，fall-through 到下一个基本块
            for (size_t idx = 0; idx < blocks.size(); ++idx) {
                if (blocks[idx].get() == block.get() && idx + 1 < blocks.size()) {
                    block->succs.push_back(blocks[idx + 1].get());
                    blocks[idx + 1]->preds.push_back(block.get());
                    break;
                }
            }
        }
    }
}

// toString：将函数序列化为 LLVM IR 文本（含函数签名、基本块、指令）
std::string Function::toString() const {
    std::string retTy = (returnType == "void") ? "void" : "i32";
    std::string s = "define dso_local " + retTy + " @" + name + "(";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0)
            s += ", ";
        s += "i32 noundef %" + params[i].name;
    }
    s += ") #0 {\n";

    for (size_t bi = 0; bi < blocks.size(); ++bi) {
        auto &bb = blocks[bi];
        if (bi > 0) {
            // 非入口块输出标签
            s += "\n" + bb->name + ":\n";
        }
        for (auto &inst : bb->insts) {
            s += "  " + inst->toString() + "\n";
        }
    }
    s += "}\n";
    return s;
}

#pragma endregion

#pragma region 模块 Module

// toString：将模块序列化为完整的 LLVM IR 文本（含模块头、所有函数）
std::string Module::toString() const {
    std::string s;
    s += "; ModuleID = '" + name + "'\n";
    s += "source_filename = \"" + sourceFile + "\"\n";
    s += "target triple = \"" + targetTriple + "\"\n\n\n";
    for (auto &func : functions) {
        s += func->toString() + "\n";
    }
    return s;
}

#pragma endregion

} // namespace ir
} // namespace toyc
