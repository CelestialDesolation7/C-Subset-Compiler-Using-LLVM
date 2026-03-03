#pragma once
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#pragma region 节点基类//--------------------------------------------------------
// 抽象语法树（AST）所有节点的抽象基类
//--------------------------------------------------------
struct ASTNode {
    virtual ~ASTNode() = default; // 虚析构，确保派生类正确析构
    virtual void
    print(int indent,
          std::ostream &os = std::cout) const = 0; // 纯虚函数：打印节点，indent 表示缩进级别
};

// 智能指针别名：指向 ASTNode 的共享所有权指针
using ASTPtr = std::shared_ptr<ASTNode>;
#pragma endregion

#pragma region 表达式节点
//--------------------------------------------------------
// 表达式基类（继承自 ASTNode）
//--------------------------------------------------------
struct Expr : ASTNode {};

// 数字字面量表达式节点
struct NumberExpr : Expr {
    int value;                                                           // 存储数字值
    explicit NumberExpr(int v) : value(v) {}                             // 构造时初始化数字
    void print(int indent, std::ostream &os = std::cout) const override; // 按缩进打印数字
};

// 标识符表达式节点
struct IdentifierExpr : Expr {
    std::string name;                                                    // 变量名或函数名
    explicit IdentifierExpr(std::string n) : name(std::move(n)) {}       // 构造时移动字符串
    void print(int indent, std::ostream &os = std::cout) const override; // 按缩进打印标识符
};

// 二元运算表达式节点
struct BinaryExpr : Expr {
    std::string op;  // 运算符（如 "+", "-", "==", "!=" 等）
    ASTPtr lhs, rhs; // 左右子表达式
    BinaryExpr(std::string o, ASTPtr l, ASTPtr r)
        : op(std::move(o)), lhs(std::move(l)), rhs(std::move(r)) {}
    void print(int indent, std::ostream &os = std::cout) const override; // 按缩进打印运算及子节点
};

// 一元运算表达式节点
struct UnaryExpr : Expr {
    std::string op; // 运算符（如 "-", "!"）
    ASTPtr expr;    // 作用对象表达式
    UnaryExpr(std::string o, ASTPtr e) : op(std::move(o)), expr(std::move(e)) {}
    void print(int indent,
               std::ostream &os = std::cout) const override; // 按缩进打印运算符及子表达式
};

// 函数调用表达式节点
struct CallExpr : Expr {
    std::string callee;                                        // 被调用函数名称
    std::vector<ASTPtr> args;                                  // 参数列表
    explicit CallExpr(std::string c) : callee(std::move(c)) {} // 构造时初始化函数名
    void print(int indent, std::ostream &os = std::cout) const override; // 按缩进打印调用信息及参数
};
#pragma endregion

#pragma region 语句节点
//--------------------------------------------------------
// 语句基类（继承自 ASTNode）
//--------------------------------------------------------
struct Stmt : ASTNode {};

// 赋值语句节点
struct AssignStmt : Stmt {
    std::string name; // 被赋值变量名
    ASTPtr expr;      // 右值表达式
    AssignStmt(std::string n, ASTPtr e) : name(std::move(n)), expr(std::move(e)) {}
    void print(int indent, std::ostream &os = std::cout) const override; // 打印赋值语句
};

// 声明语句节点（int x = expr;）
struct DeclStmt : Stmt {
    std::string name; // 声明变量名
    ASTPtr expr;      // 初始化表达式
    DeclStmt(std::string n, ASTPtr e) : name(std::move(n)), expr(std::move(e)) {}
    void print(int indent, std::ostream &os = std::cout) const override; // 打印声明语句
};

// if 语句节点，包括可选的 else 分支
struct IfStmt : Stmt {
    ASTPtr cond;     // 条件表达式
    ASTPtr thenStmt; // then 分支语句
    ASTPtr elseStmt; // else 分支语句（可空）
    IfStmt(ASTPtr c, ASTPtr t, ASTPtr e)
        : cond(std::move(c)), thenStmt(std::move(t)), elseStmt(std::move(e)) {}
    void print(int indent, std::ostream &os = std::cout) const override; // 打印 if/else 结构
};

// while 循环语句节点
struct WhileStmt : Stmt {
    ASTPtr cond; // 循环条件
    ASTPtr body; // 循环体语句
    WhileStmt(ASTPtr c, ASTPtr b) : cond(std::move(c)), body(std::move(b)) {}
    void print(int indent, std::ostream &os = std::cout) const override; // 打印 while 结构
};

// break 语句节点
struct BreakStmt : Stmt {
    void print(int indent, std::ostream &os = std::cout) const override; // 按缩进打印 "Break"
};

// continue 语句节点
struct ContinueStmt : Stmt {
    void print(int indent, std::ostream &os = std::cout) const override; // 按缩进打印 "Continue"
};

// 返回语句节点
struct ReturnStmt : Stmt {
    ASTPtr expr; // 返回值表达式（可空，void 函数无返回值）
    explicit ReturnStmt(ASTPtr e) : expr(std::move(e)) {}
    void print(int indent,
               std::ostream &os = std::cout) const override; // 按缩进打印 return 及表达式
};

// 语句块节点：表示大括号 {} 中的多条语句
struct BlockStmt : Stmt {
    std::vector<ASTPtr> stmts;                                           // 内部语句列表
    void print(int indent, std::ostream &os = std::cout) const override; // 打印块及内部所有语句
};
#pragma endregion

#pragma region 函数定义节点
//--------------------------------------------------------
// 函数定义节点
//--------------------------------------------------------

// 函数参数结构：仅包含参数名（类型统一为 int）
struct Param {
    std::string name; // 参数名称
};

// 函数定义节点：包含返回类型、函数名、参数列表和函数体
struct FuncDef : ASTNode {
    std::string retType;             // 返回类型（"int" 或 "void"）
    std::string name;                // 函数名称
    std::vector<Param> params;       // 参数列表
    std::shared_ptr<BlockStmt> body; // 函数体
    void print(int indent, std::ostream &os = std::cout) const override; // 打印函数签名及函数体
};
#pragma endregion
