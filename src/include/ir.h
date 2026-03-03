#pragma once
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace toyc {
namespace ir {

#pragma region 指令操作码 Opcode

// Opcode 枚举：表示所有 IR 指令的操作类型
enum class Opcode {
    // 内存操作
    Alloca, // %result = alloca type          — 在栈上分配空间
    Load,   // %result = load type, ptr %ptr  — 从内存加载值
    Store,  // store type value, ptr %ptr     — 将值存入内存

    // 算术运算（二元）
    Add,  // 加法
    Sub,  // 减法
    Mul,  // 乘法
    SDiv, // 有符号除法
    SRem, // 有符号取模

    // 比较运算
    ICmp, // %result = icmp pred type lhs, rhs — 整数比较

    // 控制流
    Br,      // br label %target                           — 无条件跳转
    CondBr,  // br i1 %cond, label %true, label %false     — 条件分支
    Ret,     // ret type value                              — 带返回值返回
    RetVoid, // ret void                                    — 无返回值返回

    // 函数调用
    Call, // %result = call type @func(args...) — 函数调用
};

// opcodeToString：将 Opcode 转换为 LLVM IR 文本表示（如 "add", "br", "ret"）
std::string opcodeToString(Opcode op);

// stringToArithOpcode：从算术操作名解析 Opcode（"add","sub","mul","sdiv","srem"）
Opcode stringToArithOpcode(const std::string &s);

#pragma endregion

#pragma region 比较谓词 CmpPred

// CmpPred 枚举：表示 icmp 指令的比较谓词
enum class CmpPred {
    EQ,  // 等于
    NE,  // 不等于
    SLT, // 有符号小于
    SGT, // 有符号大于
    SLE, // 有符号小于等于
    SGE  // 有符号大于等于
};

// cmpPredToString：将比较谓词转换为文本（如 "eq", "slt"）
std::string cmpPredToString(CmpPred pred);

// stringToCmpPred：从文本解析比较谓词
CmpPred stringToCmpPred(const std::string &s);

#pragma endregion

#pragma region 操作数 Operand

// Operand 类：表示 IR 指令的操作数，可以是虚拟寄存器、立即数、标签或布尔字面量
class Operand {
  public:
    // 操作数类型枚举
    enum class Kind {
        None,   // 空操作数
        VReg,   // 虚拟寄存器（如 %1, %2）
        Imm,    // 立即数（如 42）
        Label,  // 基本块标签（如 %entry, %if.then）
        BoolLit // 布尔字面量（true/false）
    };

    // -------- 静态工厂方法 --------
    static Operand none() { // 创建空操作数
        Operand o;
        return o;
    }
    static Operand vreg(int id) { // 创建虚拟寄存器操作数
        Operand o;
        o.kind_ = Kind::VReg;
        o.value_ = id;
        return o;
    }
    static Operand imm(int val) { // 创建立即数操作数
        Operand o;
        o.kind_ = Kind::Imm;
        o.value_ = val;
        return o;
    }
    static Operand label(const std::string &name) { // 创建标签操作数
        Operand o;
        o.kind_ = Kind::Label;
        o.label_ = name;
        return o;
    }
    static Operand boolLit(bool val) { // 创建布尔字面量操作数
        Operand o;
        o.kind_ = Kind::BoolLit;
        o.value_ = val ? 1 : 0;
        return o;
    }

    // -------- 类型判断 --------
    Kind kind() const { return kind_; }
    bool isNone() const { return kind_ == Kind::None; }
    bool isVReg() const { return kind_ == Kind::VReg; }
    bool isImm() const { return kind_ == Kind::Imm; }
    bool isLabel() const { return kind_ == Kind::Label; }
    bool isBoolLit() const { return kind_ == Kind::BoolLit; }

    // -------- 值访问 --------
    int regId() const { return value_; }                    // 获取虚拟寄存器 ID
    int immValue() const { return value_; }                 // 获取立即数值
    bool boolValue() const { return value_ != 0; }          // 获取布尔值
    const std::string &labelName() const { return label_; } // 获取标签名

    // toString：序列化为 LLVM IR 文本（如 "%1", "42", "true", "%entry"）
    std::string toString() const;

  private:
    Kind kind_ = Kind::None; // 操作数种类
    int value_ = 0;          // 整数值（寄存器ID / 立即数 / 布尔值）
    std::string label_;      // 标签名称（仅 Label 类型使用）
};

#pragma endregion

#pragma region 指令 Instruction

// Instruction 类：表示一条 IR 指令，包含操作码、操作数、结果定义等信息
class Instruction {
  public:
    Opcode opcode;                 // 指令操作码
    std::string type;              // 操作类型（"i32", "i1", "void"）
    Operand def;                   // 结果寄存器（若无定义则为 None）
    std::vector<Operand> ops;      // 操作数列表
    CmpPred cmpPred = CmpPred::EQ; // 比较谓词（仅 ICmp 指令使用）
    std::string callee;            // 被调用函数名（仅 Call 指令使用）
    bool nsw = false;              // no-signed-wrap 标志（算术运算使用）
    int align = 4;                 // 内存对齐（Alloca/Load/Store 使用）

    int index = -1;   // 线性化后的全局顺序编号（用于活跃性分析）
    int blockId = -1; // 所属基本块 ID

    // -------- 工厂方法：用于创建各类型 IR 指令 --------
    static Instruction makeAlloca(Operand def, const std::string &type, int align = 4);
    static Instruction makeLoad(Operand def, const std::string &type, Operand ptr, int align = 4);
    static Instruction makeStore(const std::string &type, Operand value, Operand ptr,
                                 int align = 4);
    static Instruction makeBinOp(Opcode op, Operand def, const std::string &type, Operand lhs,
                                 Operand rhs);
    static Instruction makeICmp(CmpPred pred, Operand def, const std::string &type, Operand lhs,
                                Operand rhs);
    static Instruction makeBr(Operand target);
    static Instruction makeCondBr(Operand cond, Operand trueTarget, Operand falseTarget);
    static Instruction makeRet(const std::string &type, Operand value);
    static Instruction makeRetVoid();
    static Instruction makeCall(Operand def, const std::string &retType, const std::string &callee,
                                std::vector<Operand> args);

    // -------- 查询接口（基于 opcode 判断，无需正则匹配）--------

    // defReg：返回指令定义（写入）的虚拟寄存器 ID，若无定义返回 -1
    int defReg() const;

    // useRegs：返回指令使用（读取）的所有虚拟寄存器 ID
    std::vector<int> useRegs() const;

    // isTerminator：判断是否为终结指令（br/condbr/ret/retvoid）
    bool isTerminator() const;

    // isCallInst：判断是否为函数调用指令
    bool isCallInst() const;

    // branchTargets：返回分支目标基本块标签列表
    std::vector<std::string> branchTargets() const;

    // branchCondReg：返回条件分支的条件寄存器 ID，非条件分支返回 -1
    int branchCondReg() const;

    // posDef/posUse：用于线性扫描寄存器分配的位置计算
    // 每条指令对应两个位置点：偶数为写入点，奇数为使用点
    int posDef() const { return index * 2; }
    int posUse() const { return index * 2 + 1; }

    // toString：将指令序列化为 LLVM IR 文本行
    std::string toString() const;
};

#pragma endregion

#pragma region 基本块 BasicBlock

// BasicBlock 类：表示控制流图中的一个基本块
class BasicBlock {
  public:
    int id = -1;      // 基本块编号
    std::string name; // 基本块标签名（如 "entry", "if.then"）
    std::vector<std::unique_ptr<Instruction>> insts; // 指令列表

    std::vector<BasicBlock *> succs; // 后继基本块列表
    std::vector<BasicBlock *> preds; // 前驱基本块列表

    // 活跃性分析数据（用于寄存器分配）
    std::set<int> defSet, useSet;  // 基本块内定义和使用的寄存器集合
    std::set<int> liveIn, liveOut; // 块入口和出口的活跃寄存器集合

    // firstPos/lastPos：返回块内第一条/最后一条指令的位置（用于活跃区间计算）
    int firstPos() const;
    int lastPos() const;
};

#pragma endregion

#pragma region 函数参数与函数

// FuncParam 结构体：表示函数形参
struct FuncParam {
    std::string name;         // 参数名（或形如 "0", "1" 的索引字符串）
    std::string type = "i32"; // 参数类型（ToyC 中统一为 i32）
};

// ======================== 函数 ========================

// Function 类：表示一个 IR 函数，包含参数、基本块、CFG 信息
class Function {
  public:
    std::string name;                                       // 函数名称
    std::string returnType;                                 // 返回类型（"i32" 或 "void"）
    std::vector<FuncParam> params;                          // 形参列表
    std::vector<std::unique_ptr<BasicBlock>> blocks;        // 基本块列表（按序）
    std::unordered_map<std::string, BasicBlock *> blockMap; // 标签名 → 基本块的快速查找表
    std::vector<BasicBlock *> rpoOrder; // 逆后序遍历顺序（用于数据流分析）
    std::vector<int> paramVregs;        // 函数参数对应的虚拟寄存器 ID
    int maxVregId = -1;                 // 最大虚拟寄存器编号

    // buildCFG：根据分支指令构建控制流图（计算 succs/preds）
    void buildCFG();

    // entryBlock：返回函数入口基本块
    BasicBlock *entryBlock() const;

    // toString：将函数序列化为 LLVM IR 文本
    std::string toString() const;
};

#pragma endregion

#pragma region 模块 Module

// Module 类：表示整个编译单元，包含所有函数定义
class Module {
  public:
    std::string name = "toyc";                        // 模块名
    std::string sourceFile = "toyc";                  // 源文件名
    std::string targetTriple = "riscv32-unknown-elf"; // 目标三元组
    std::vector<std::unique_ptr<Function>> functions; // 函数定义列表

    // toString：将模块序列化为完整的 LLVM IR 文本
    std::string toString() const;
};

#pragma endregion

} // namespace ir
} // namespace toyc
