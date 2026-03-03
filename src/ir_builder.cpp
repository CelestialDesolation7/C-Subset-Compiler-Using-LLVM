#include "ir_builder.h"
#include <algorithm>

namespace toyc {

using namespace ir;

// 构造函数：初始化并进入第一层作用域
IRBuilder::IRBuilder() { enterScope(); }

#pragma region 辅助方法

// newVReg：分配并返回一个新的虚拟寄存器操作数
Operand IRBuilder::newVReg() { return Operand::vreg(++vregCounter_); }

// newLabel：生成新的唯一标签名（如 "then_0", "while_cond_1"）
std::string IRBuilder::newLabel(const std::string &base) {
    return base + "_" + std::to_string(labelCounter_);
}

// createBlock：创建新的基本块并加入当前函数，返回其指针
BasicBlock *IRBuilder::createBlock(const std::string &name) {
    auto bb = std::make_unique<BasicBlock>();
    bb->id = static_cast<int>(currentFunc_->blocks.size());
    bb->name = name;
    auto *ptr = bb.get();
    currentFunc_->blockMap[name] = ptr;
    currentFunc_->blocks.push_back(std::move(bb));
    return ptr;
}

// setInsertBlock：设置当前指令插入点为指定基本块
void IRBuilder::setInsertBlock(BasicBlock *bb) { currentBB_ = bb; }

// emit：将一条指令发射到当前基本块末尾
void IRBuilder::emit(Instruction inst) {
    auto p = std::make_unique<Instruction>(std::move(inst));
    p->blockId = currentBB_->id;
    currentBB_->insts.push_back(std::move(p));
}

#pragma endregion

#pragma region 作用域管理

// enterScope：进入新作用域（压入新的变量映射表）
void IRBuilder::enterScope() { scopeStack_.emplace_back(); }

// exitScope：退出当前作用域（弹出栈顶变量映射表）
void IRBuilder::exitScope() {
    if (!scopeStack_.empty())
        scopeStack_.pop_back();
}

// addVariable：在当前作用域添加变量（变量名 → alloca 寄存器）
void IRBuilder::addVariable(const std::string &name, const Operand &allocaReg) {
    if (!scopeStack_.empty())
        scopeStack_.back()[name] = allocaReg;
}

// findVariable：从当前作用域开始向外层查找变量，实现变量隐藏语义
Operand IRBuilder::findVariable(const std::string &name) {
    for (int i = static_cast<int>(scopeStack_.size()) - 1; i >= 0; --i) {
        auto it = scopeStack_[i].find(name);
        if (it != scopeStack_[i].end())
            return it->second;
    }
    return Operand::none();
}

#pragma endregion

#pragma region 模块与函数构建

// buildModule：生成完整的 IR 模块，遍历所有函数定义并逐个生成
std::unique_ptr<Module> IRBuilder::buildModule(const std::vector<std::shared_ptr<FuncDef>> &funcs) {
    auto mod = std::make_unique<Module>();
    module_ = mod.get();
    for (auto &f : funcs)
        buildFunction(f);
    return mod;
}

// buildFunction：为单个函数生成 IR
// 1. 重置计数器和作用域
// 2. 创建入口基本块
// 3. 处理参数的 alloca + store
// 4. 遍历函数体生成指令
// 5. 添加默认返回（若未显式 return）
void IRBuilder::buildFunction(const std::shared_ptr<FuncDef> &funcDef) {
    // 重置状态
    labelCounter_ = 0;
    vregCounter_ = static_cast<int>(funcDef->params.size());
    scopeStack_.clear();
    enterScope();
    loadedValues_.clear();
    breakLabels_.clear();
    continueLabels_.clear();
    currentFuncName_ = funcDef->name;
    hasReturn_ = false;
    isMainFunction_ = (funcDef->name == "main");

    auto func = std::make_unique<Function>();
    func->name = funcDef->name;
    func->returnType = funcDef->retType;
    currentFunc_ = func.get();

    // 保存原始参数名，将 AST 参数名改为索引
    std::vector<std::string> origNames;
    for (auto &p : funcDef->params)
        origNames.push_back(p.name);
    for (size_t i = 0; i < funcDef->params.size(); ++i)
        funcDef->params[i].name = std::to_string(i);

    // 设置函数参数信息
    for (size_t i = 0; i < funcDef->params.size(); ++i) {
        func->params.push_back({funcDef->params[i].name, "i32"});
        func->paramVregs.push_back(static_cast<int>(i));
    }

    // 创建入口基本块
    auto *entryBB = createBlock("entry");
    setInsertBlock(entryBB);

    // main 函数的返回值 alloca
    if (isMainFunction_) {
        Operand retVar = newVReg();
        addVariable(funcDef->name + "_ret", retVar);
        emit(Instruction::makeAlloca(retVar, "i32"));
        emit(Instruction::makeStore("i32", Operand::imm(0), retVar));
    }

    // 参数 alloca + store
    for (size_t i = 0; i < funcDef->params.size(); ++i) {
        std::string irName = funcDef->params[i].name;
        std::string orig = origNames[i];
        Operand slot = newVReg();
        emit(Instruction::makeAlloca(slot, "i32"));
        emit(Instruction::makeStore("i32", Operand::vreg(static_cast<int>(i)), slot));
        addVariable(irName, slot);
        addVariable(orig, slot);
    }

    // 生成函数体
    buildBlock(funcDef->body);

    // 添加默认返回
    if (!hasReturn_) {
        if (funcDef->retType == "int")
            emit(Instruction::makeRet("i32", Operand::imm(0)));
        else
            emit(Instruction::makeRetVoid());
    }

    func->maxVregId = vregCounter_;
    module_->functions.push_back(std::move(func));
}

#pragma endregion

#pragma region 语句生成

// buildBlock：生成语句块 IR，进入新作用域后遍历所有语句
void IRBuilder::buildBlock(const std::shared_ptr<BlockStmt> &block) {
    enterScope();
    for (auto &stmt : block->stmts)
        buildStmt(stmt);
    exitScope();
}

// buildStmt：根据语句的实际类型 dispatch 到对应的生成方法
void IRBuilder::buildStmt(const ASTPtr &stmt) {
    if (!stmt)
        return;
    if (auto s = std::dynamic_pointer_cast<AssignStmt>(stmt)) {
        buildAssign(s);
        return;
    }
    if (auto s = std::dynamic_pointer_cast<DeclStmt>(stmt)) {
        buildDecl(s);
        return;
    }
    if (auto s = std::dynamic_pointer_cast<IfStmt>(stmt)) {
        buildIf(s);
        return;
    }
    if (auto s = std::dynamic_pointer_cast<WhileStmt>(stmt)) {
        buildWhile(s);
        return;
    }
    if (auto s = std::dynamic_pointer_cast<ReturnStmt>(stmt)) {
        buildReturn(s);
        return;
    }
    if (auto s = std::dynamic_pointer_cast<BreakStmt>(stmt)) {
        buildBreak();
        return;
    }
    if (auto s = std::dynamic_pointer_cast<ContinueStmt>(stmt)) {
        buildContinue();
        return;
    }
    if (auto s = std::dynamic_pointer_cast<BlockStmt>(stmt)) {
        buildBlock(s);
        return;
    }
    // 表达式语句（含函数调用语句）
    if (auto s = std::dynamic_pointer_cast<CallExpr>(stmt)) {
        buildCall(s);
        return;
    }
    if (auto s = std::dynamic_pointer_cast<Expr>(stmt)) {
        buildExpr(s);
        return;
    }
}

// buildAssign：生成赋值语句 IR（计算右值并 store 到变量地址）
void IRBuilder::buildAssign(const std::shared_ptr<AssignStmt> &assign) {
    Operand value = buildExpr(assign->expr);
    Operand varOp = findVariable(assign->name);
    if (!varOp.isNone()) {
        emit(Instruction::makeStore("i32", value, varOp));
        loadedValues_.erase(assign->name);
    }
}

// buildDecl：生成声明语句 IR（alloca + store，并将变量注册到当前作用域）
void IRBuilder::buildDecl(const std::shared_ptr<DeclStmt> &decl) {
    Operand val = buildExpr(decl->expr);
    Operand slot = newVReg();
    emit(Instruction::makeAlloca(slot, "i32"));
    addVariable(decl->name, slot);
    emit(Instruction::makeStore("i32", val, slot));
    loadedValues_.erase(decl->name);
}

// buildIf：生成 if 语句 IR
// 创建 then/else/endif 三个基本块，通过条件分支连接
void IRBuilder::buildIf(const std::shared_ptr<IfStmt> &ifStmt) {
    loadedValues_.clear(); // 进入分支前清除缓存
    Operand cond = buildExpr(ifStmt->cond);

    std::string thenName = newLabel("then");
    std::string elseName = newLabel("else");
    std::string endName = newLabel("endif");
    labelCounter_++;

    emit(Instruction::makeCondBr(cond, Operand::label(thenName), Operand::label(elseName)));

    // Then 块
    auto *thenBB = createBlock(thenName);
    setInsertBlock(thenBB);
    loadedValues_.clear();
    buildStmt(ifStmt->thenStmt);
    emit(Instruction::makeBr(Operand::label(endName)));

    // Else 块
    auto *elseBB = createBlock(elseName);
    setInsertBlock(elseBB);
    loadedValues_.clear();
    buildStmt(ifStmt->elseStmt);
    emit(Instruction::makeBr(Operand::label(endName)));

    // Merge 块 —— 来自不同分支，缓存的 load 值无效
    auto *endBB = createBlock(endName);
    setInsertBlock(endBB);
    loadedValues_.clear();
}

// buildWhile：生成 while 循环 IR
// 创建 cond/body/end 三个基本块，循环体末尾跳回 cond 块
void IRBuilder::buildWhile(const std::shared_ptr<WhileStmt> &whileStmt) {
    std::string condName = newLabel("while_cond");
    std::string bodyName = newLabel("while_body");
    std::string endName = newLabel("while_end");
    labelCounter_++;

    breakLabels_.push_back(endName);
    continueLabels_.push_back(condName);

    emit(Instruction::makeBr(Operand::label(condName)));

    // 条件块
    auto *condBB = createBlock(condName);
    setInsertBlock(condBB);
    loadedValues_.clear();
    Operand cond = buildExpr(whileStmt->cond);
    emit(Instruction::makeCondBr(cond, Operand::label(bodyName), Operand::label(endName)));

    // 循环体
    auto *bodyBB = createBlock(bodyName);
    setInsertBlock(bodyBB);
    loadedValues_.clear();
    buildStmt(whileStmt->body);
    emit(Instruction::makeBr(Operand::label(condName)));

    // 循环出口
    auto *endBB = createBlock(endName);
    setInsertBlock(endBB);

    breakLabels_.pop_back();
    continueLabels_.pop_back();
}
#pragma endregion

#pragma region 语句生成
// buildReturn：生成返回语句 IR，根据是否有返回值选择 ret/retvoid
void IRBuilder::buildReturn(const std::shared_ptr<ReturnStmt> &retStmt) {
    if (retStmt->expr) {
        Operand value = buildExpr(retStmt->expr);
        emit(Instruction::makeRet("i32", value));
    } else {
        emit(Instruction::makeRetVoid());
    }
    hasReturn_ = true;
}

// buildBreak：生成 break 语句 IR（跳转到最近的循环出口块）
void IRBuilder::buildBreak() {
    if (!breakLabels_.empty())
        emit(Instruction::makeBr(Operand::label(breakLabels_.back())));
}

// buildContinue：生成 continue 语句 IR（跳转到最近的循环条件块）
void IRBuilder::buildContinue() {
    if (!continueLabels_.empty())
        emit(Instruction::makeBr(Operand::label(continueLabels_.back())));
}

#pragma endregion

#pragma region 表达式生成

// buildExpr：生成表达式 IR，返回结果操作数
// 根据表达式类型 dispatch：数字返回立即数，标识符先查缓存再 load，二元/一元/调用递归处理
Operand IRBuilder::buildExpr(const ASTPtr &expr) {
    if (auto e = std::dynamic_pointer_cast<NumberExpr>(expr))
        return Operand::imm(e->value);

    if (auto e = std::dynamic_pointer_cast<IdentifierExpr>(expr)) {
        Operand varOp = findVariable(e->name);
        if (!varOp.isNone()) {
            auto it = loadedValues_.find(e->name);
            if (it != loadedValues_.end())
                return it->second;
            Operand temp = newVReg();
            emit(Instruction::makeLoad(temp, "i32", varOp));
            loadedValues_[e->name] = temp;
            return temp;
        }
        // 仅当名字是纯数字时 (函数参数索引) 才用 stoi
        bool isNumeric = !e->name.empty() && std::all_of(e->name.begin(), e->name.end(), ::isdigit);
        if (isNumeric)
            return Operand::vreg(std::stoi(e->name)); // 直接引用参数寄存器
        std::cerr << "Error: undefined variable '" << e->name << "'\n";
        return Operand::imm(0);
    }

    if (auto e = std::dynamic_pointer_cast<BinaryExpr>(expr))
        return buildBinaryOp(e->op, e->lhs, e->rhs);

    if (auto e = std::dynamic_pointer_cast<UnaryExpr>(expr))
        return buildUnaryOp(e->op, e->expr);

    if (auto e = std::dynamic_pointer_cast<CallExpr>(expr))
        return buildCall(e);

    return Operand::imm(0);
}

// buildBinaryOp：生成二元运算 IR
// 逻辑运算和比较运算分别委托给专用方法，算术运算直接生成 binop 指令
Operand IRBuilder::buildBinaryOp(const std::string &op, const ASTPtr &lhs, const ASTPtr &rhs) {
    if (op == "&&" || op == "||")
        return buildLogicalOp(op, lhs, rhs);
    if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=")
        return buildComparison(op, lhs, rhs);

    Operand lhsOp = buildExpr(lhs);
    Operand rhsOp = buildExpr(rhs);
    Operand result = newVReg();

    Opcode opc;
    if (op == "+")
        opc = Opcode::Add;
    else if (op == "-")
        opc = Opcode::Sub;
    else if (op == "*")
        opc = Opcode::Mul;
    else if (op == "/")
        opc = Opcode::SDiv;
    else
        opc = Opcode::SRem;

    emit(Instruction::makeBinOp(opc, result, "i32", lhsOp, rhsOp));
    return result;
}

// buildUnaryOp：生成一元运算 IR
// '-' 生成 sub 0, x；'!' 生成 icmp eq x, 0；'+' 无操作
Operand IRBuilder::buildUnaryOp(const std::string &op, const ASTPtr &expr) {
    if (op == "-") {
        if (auto num = std::dynamic_pointer_cast<NumberExpr>(expr))
            return Operand::imm(-num->value);
        Operand inner = buildExpr(expr);
        Operand result = newVReg();
        emit(Instruction::makeBinOp(Opcode::Sub, result, "i32", Operand::imm(0), inner));
        return result;
    }
    if (op == "!") {
        Operand inner = buildExpr(expr);
        Operand result = newVReg();
        emit(Instruction::makeICmp(CmpPred::EQ, result, "i32", inner, Operand::imm(0)));
        return result;
    }
    // "+" — 无操作
    return buildExpr(expr);
}

// buildComparison：生成比较运算 IR，输出 icmp 指令
Operand IRBuilder::buildComparison(const std::string &op, const ASTPtr &lhs, const ASTPtr &rhs) {
    Operand lhsOp = buildExpr(lhs);
    Operand rhsOp = buildExpr(rhs);
    Operand result = newVReg();

    CmpPred pred;
    if (op == "==")
        pred = CmpPred::EQ;
    else if (op == "!=")
        pred = CmpPred::NE;
    else if (op == "<")
        pred = CmpPred::SLT;
    else if (op == ">")
        pred = CmpPred::SGT;
    else if (op == "<=")
        pred = CmpPred::SLE;
    else
        pred = CmpPred::SGE;

    emit(Instruction::makeICmp(pred, result, "i32", lhsOp, rhsOp));
    return result;
}

// buildLogicalOp：生成逻辑运算 IR（&& / ||），实现短路语义
// 通过条件分支实现：
//   && — lhs 为 false 则短路，结果为 false
//   || — lhs 为 true 则短路，结果为 true
// 结果通过 alloca i1 存储，最后 load 出来作为返回值
Operand IRBuilder::buildLogicalOp(const std::string &op, const ASTPtr &lhs, const ASTPtr &rhs) {
    // 为短路逻辑分配结果变量
    Operand resultVar = newVReg();
    emit(Instruction::makeAlloca(resultVar, "i1", 1));

    Operand lhsOp = buildExpr(lhs);

    if (op == "&&") {
        std::string rhsName = newLabel("land_rhs");
        std::string falseName = newLabel("land_false");
        std::string endName = newLabel("land_end");
        labelCounter_++;

        emit(Instruction::makeCondBr(lhsOp, Operand::label(rhsName), Operand::label(falseName)));

        // false 块
        auto *falseBB = createBlock(falseName);
        setInsertBlock(falseBB);
        emit(Instruction::makeStore("i1", Operand::boolLit(false), resultVar, 1));
        emit(Instruction::makeBr(Operand::label(endName)));

        // rhs 块
        auto *rhsBB = createBlock(rhsName);
        setInsertBlock(rhsBB);
        Operand rhsOp = buildExpr(rhs);
        emit(Instruction::makeStore("i1", rhsOp, resultVar, 1));
        emit(Instruction::makeBr(Operand::label(endName)));

        // end 块
        auto *endBB = createBlock(endName);
        setInsertBlock(endBB);
    } else { // "||"
        std::string trueName = newLabel("lor_true");
        std::string rhsName = newLabel("lor_rhs");
        std::string endName = newLabel("lor_end");
        labelCounter_++;

        emit(Instruction::makeCondBr(lhsOp, Operand::label(trueName), Operand::label(rhsName)));

        // true 块
        auto *trueBB = createBlock(trueName);
        setInsertBlock(trueBB);
        emit(Instruction::makeStore("i1", Operand::boolLit(true), resultVar, 1));
        emit(Instruction::makeBr(Operand::label(endName)));

        // rhs 块
        auto *rhsBB = createBlock(rhsName);
        setInsertBlock(rhsBB);
        Operand rhsOp = buildExpr(rhs);
        emit(Instruction::makeStore("i1", rhsOp, resultVar, 1));
        emit(Instruction::makeBr(Operand::label(endName)));

        // end 块
        auto *endBB = createBlock(endName);
        setInsertBlock(endBB);
    }

    Operand result = newVReg();
    emit(Instruction::makeLoad(result, "i1", resultVar, 1));
    return result;
}

// buildCall：生成函数调用 IR，先生成所有实参的表达式，再发射 call 指令
Operand IRBuilder::buildCall(const std::shared_ptr<CallExpr> &call) {
    std::vector<Operand> args;
    for (auto &arg : call->args)
        args.push_back(buildExpr(arg));
    Operand result = newVReg();
    emit(Instruction::makeCall(result, "i32", call->callee, std::move(args)));
    return result;
}

#pragma endregion

#pragma region 便捷函数

// generateLLVMIR：一步完成 AST → IR 生成，返回 LLVM IR 文本
std::string generateLLVMIR(const std::vector<std::shared_ptr<FuncDef>> &funcs) {
    IRBuilder builder;
    auto mod = builder.buildModule(funcs);
    return mod->toString();
}

#pragma endregion

} // namespace toyc
