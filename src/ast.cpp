#include "ast.h"

#pragma region 辅助函数
// 缩进打印辅助函数：根据 level 层级打印相应数量的缩进空格（每层两个空格），
// 在打印 AST 节点时用于控制层次结构显示
static void printIndent(int level, std::ostream &os) {
    for (int i = 0; i < level; ++i)
        os << "  ";
}
#pragma endregion

#pragma region 表达式节点打印
// NumberExpr 打印实现：输出 Number(value)，value 为数字字面量
void NumberExpr::print(int level, std::ostream &os) const {
    printIndent(level, os);
    os << "Number(" << value << ")\n";
}

// IdentifierExpr 打印实现：输出 Identifier(name)，name 为标识符名称
void IdentifierExpr::print(int level, std::ostream &os) const {
    printIndent(level, os);
    os << "Identifier(" << name << ")\n";
}

// BinaryExpr 打印实现：
// 1. 输出 Binary(op)，op 为运算符
// 2. 递归打印 lhs 和 rhs，缩进层级加 1
void BinaryExpr::print(int level, std::ostream &os) const {
    printIndent(level, os);
    os << "Binary(" << op << ")\n";
    lhs->print(level + 1, os);
    rhs->print(level + 1, os);
}

// UnaryExpr 打印实现：输出 Unary(op)，然后打印子表达式，缩进层级加 1
void UnaryExpr::print(int level, std::ostream &os) const {
    printIndent(level, os);
    os << "Unary(" << op << ")\n";
    expr->print(level + 1, os);
}

// CallExpr 打印实现：输出 Call(callee)，callee 为调用函数名称，然后依次打印每个参数表达式
void CallExpr::print(int level, std::ostream &os) const {
    printIndent(level, os);
    os << "Call(" << callee << ")\n";
    for (auto &arg : args)
        arg->print(level + 1, os);
}
#pragma endregion

#pragma region 语句节点打印
// AssignStmt 打印实现：输出 Assign(name)，name 为被赋值变量名，然后打印赋值表达式
void AssignStmt::print(int level, std::ostream &os) const {
    printIndent(level, os);
    os << "Assign(" << name << ")\n";
    expr->print(level + 1, os);
}

// DeclStmt 打印实现：输出 Decl(name)，name 为声明变量名，然后打印初始化表达式
void DeclStmt::print(int level, std::ostream &os) const {
    printIndent(level, os);
    os << "Decl(" << name << ")\n";
    expr->print(level + 1, os);
}

// IfStmt 打印实现：
// 1. 输出 If，再打印条件和 then 分支
// 2. 如果存在 else 分支，则输出 Else 并打印 elseStmt
void IfStmt::print(int level, std::ostream &os) const {
    printIndent(level, os);
    os << "If\n";
    cond->print(level + 1, os);
    thenStmt->print(level + 1, os);
    if (elseStmt) {
        printIndent(level, os);
        os << "Else\n";
        elseStmt->print(level + 1, os);
    }
}

// WhileStmt 打印实现：输出 While，再打印循环条件和循环体
void WhileStmt::print(int level, std::ostream &os) const {
    printIndent(level, os);
    os << "While\n";
    cond->print(level + 1, os);
    body->print(level + 1, os);
}

// BreakStmt 打印实现：输出 Break
void BreakStmt::print(int level, std::ostream &os) const {
    printIndent(level, os);
    os << "Break\n";
}

// ContinueStmt 打印实现：输出 Continue
void ContinueStmt::print(int level, std::ostream &os) const {
    printIndent(level, os);
    os << "Continue\n";
}

// ReturnStmt 打印实现：输出 Return，如有返回表达式则打印该表达式
void ReturnStmt::print(int level, std::ostream &os) const {
    printIndent(level, os);
    os << "Return\n";
    if (expr)
        expr->print(level + 1, os);
}

// BlockStmt 打印实现：输出 Block，然后打印块中所有语句，缩进层级加 1
// 跳过 nullptr（空语句 `;` 解析产生的空节点）
void BlockStmt::print(int level, std::ostream &os) const {
    printIndent(level, os);
    os << "Block\n";
    for (auto &s : stmts)
        if (s)
            s->print(level + 1, os);
}
#pragma endregion

#pragma region 函数定义节点打印
// FuncDef 打印实现：
// 1. 输出 Function retType name(params)
// 2. 打印函数体 BlockStmt
void FuncDef::print(int level, std::ostream &os) const {
    printIndent(level, os);
    os << "Function " << retType << " " << name << "(";
    for (size_t i = 0; i < params.size(); ++i) {
        os << i;
        if (i + 1 < params.size())
            os << ", ";
    }
    os << ")\n";
    body->print(level + 1, os);
}
#pragma endregion
