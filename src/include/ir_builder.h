#pragma once
#include "ast.h"
#include "ir.h"
#include <map>
#include <vector>

namespace toyc {

// IRBuilder 类：将 AST 转换为结构化 LLVM IR（ir::Module）
// 采用递归下降方式遍历 AST，生成对应的 IR 指令
class IRBuilder {
#pragma region 公有接口
  public:
    // 构造函数
    IRBuilder();

    // 生成完整的模块 IR（包含所有函数）
    std::unique_ptr<ir::Module> buildModule(const std::vector<std::shared_ptr<FuncDef>> &funcs);
#pragma endregion

#pragma region 私有状态
  private:
    int vregCounter_ = 0;         // 虚拟寄存器计数器
    int labelCounter_ = 0;        // 标签计数器
    std::string currentFuncName_; // 当前处理的函数名
    bool isMainFunction_ = false; // 标记是否为 main 函数
    bool hasReturn_ = false;      // 标记函数是否已有返回语句

    ir::Module *module_ = nullptr;        // 当前模块指针
    ir::Function *currentFunc_ = nullptr; // 当前函数指针
    ir::BasicBlock *currentBB_ = nullptr; // 当前基本块（指令插入点）

    // 作用域栈：每个作用域包含变量名 → alloca 结果寄存器的映射
    std::vector<std::map<std::string, ir::Operand>> scopeStack_;
    // 已加载值缓存：变量名 → load 结果寄存器，避免重复 load
    std::map<std::string, ir::Operand> loadedValues_;

    std::vector<std::string> breakLabels_;    // break 跳转目标标签栈
    std::vector<std::string> continueLabels_; // continue 跳转目标标签栈
#pragma endregion

#pragma region 私有辅助方法

    // -------- 辅助方法 --------

    // 生成新的虚拟寄存器
    ir::Operand newVReg();
    // 生成新的标签名（base_N 格式）
    std::string newLabel(const std::string &base);
    // 创建新的基本块并加入当前函数
    ir::BasicBlock *createBlock(const std::string &name);
    // 设置当前指令插入点为指定基本块
    void setInsertBlock(ir::BasicBlock *bb);
    // 发射一条指令到当前基本块
    void emit(ir::Instruction inst);
#pragma endregion

#pragma region 私有生成方法

    // -------- 作用域管理 --------

    void enterScope(); // 进入新作用域
    void exitScope();  // 退出当前作用域
    // 在当前作用域添加变量（变量名 → alloca 寄存器）
    void addVariable(const std::string &name, const ir::Operand &allocaReg);
    // 从当前作用域开始向外查找变量
    ir::Operand findVariable(const std::string &name);

    // -------- 函数/语句/表达式生成 --------

    // 为单个函数生成 IR
    void buildFunction(const std::shared_ptr<FuncDef> &funcDef);
    // 生成语句块 IR
    void buildBlock(const std::shared_ptr<BlockStmt> &block);
    // 生成单条语句 IR（根据实际类型 dispatch）
    void buildStmt(const ASTPtr &stmt);
    // 生成表达式 IR，返回结果操作数
    ir::Operand buildExpr(const ASTPtr &expr);

    // 生成赋值语句 IR
    void buildAssign(const std::shared_ptr<AssignStmt> &assign);
    // 生成声明语句 IR
    void buildDecl(const std::shared_ptr<DeclStmt> &decl);
    // 生成 if 语句 IR（含条件分支和合并块）
    void buildIf(const std::shared_ptr<IfStmt> &ifStmt);
    // 生成 while 循环 IR（含条件块、循环体块、出口块）
    void buildWhile(const std::shared_ptr<WhileStmt> &whileStmt);
    // 生成返回语句 IR
    void buildReturn(const std::shared_ptr<ReturnStmt> &retStmt);
    // 生成 break 语句 IR（跳转到最近的循环出口）
    void buildBreak();
    // 生成 continue 语句 IR（跳转到最近的循环条件块）
    void buildContinue();

    // 生成二元运算 IR（算术/比较/逻辑由此分发）
    ir::Operand buildBinaryOp(const std::string &op, const ASTPtr &lhs, const ASTPtr &rhs);
    // 生成一元运算 IR（负号/逻辑非）
    ir::Operand buildUnaryOp(const std::string &op, const ASTPtr &expr);
    // 生成比较运算 IR（==, !=, <, >, <=, >=）
    ir::Operand buildComparison(const std::string &op, const ASTPtr &lhs, const ASTPtr &rhs);
    // 生成逻辑运算 IR（&& / ||，实现短路语义）
    ir::Operand buildLogicalOp(const std::string &op, const ASTPtr &lhs, const ASTPtr &rhs);
    // 生成函数调用 IR
    ir::Operand buildCall(const std::shared_ptr<CallExpr> &call);
#pragma endregion
};

// 便捷函数：从 AST 生成 LLVM IR 文本
std::string generateLLVMIR(const std::vector<std::shared_ptr<FuncDef>> &funcs);

} // namespace toyc
