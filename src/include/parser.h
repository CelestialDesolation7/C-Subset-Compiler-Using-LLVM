#pragma once
#include "ast.h"
#include "lexer.h"

// Parser 类：将 Token 流解析为 AST
class Parser {
#pragma region 公有接口
  public:
    // 构造函数：初始化 Lexer 并预读两个 Token
    explicit Parser(const std::string &source);

    // 解析编译单元 CompUnit → FuncDef+
    std::vector<std::shared_ptr<FuncDef>> parseCompUnit();
#pragma endregion

#pragma region 私有状态与工具方法
  private:
    Lexer lex;      // 词法分析器实例
    Token cur, nxt; // 当前和下一个 Token，用于预读实现

    // advance：将 nxt 赋值给 cur，然后读取下一个 Token 到 nxt
    void advance();

    // match：如果 cur.type 与 expected 相符，则 advance 并返回 true
    bool match(TokenType expected);

    // expect：断言当前 Token 类型为 expected，否则报告错误并退出
    void expect(TokenType expected);
#pragma endregion

#pragma region 私有解析方法
    // 解析函数定义 FuncDef → ("int" ∣ "void") ID "(" (Param ("," Param)∗)? ")" Block
    std::shared_ptr<FuncDef> parseFuncDef();

    // 解析形参列表 Param → "int" ID ("," "int" ID)*
    std::vector<Param> parseParams();

    // 解析语句块 Block → "{" Stmt∗ "}"
    std::shared_ptr<BlockStmt> parseBlock();

    // 解析单条语句 Stmt → 声明|赋值|if|while|return|break|continue|表达式
    ASTPtr parseStmt();

    // 解析表达式 Expr → LOrExpr
    ASTPtr parseExpr();

    // 解析逻辑或 LOrExpr → LAndExpr ∣ LOrExpr "||" LAndExpr
    ASTPtr parseLOr();

    // 解析逻辑与 LAndExpr → RelExpr ∣ LAndExpr "&&" RelExpr
    ASTPtr parseLAnd();

    // 解析关系运算 RelExpr → AddExpr ∣ RelExpr ("<" ∣ ">" ∣ "<=" ∣ ">=" ∣ "==" ∣ "!=") AddExpr
    ASTPtr parseRel();

    // 解析加减运算 AddExpr → MulExpr ∣ AddExpr ("+" ∣ "-") MulExpr
    ASTPtr parseAdd();

    // 解析乘除模 MulExpr → UnaryExpr ∣ MulExpr ("*" ∣ "/" ∣ "%") UnaryExpr
    ASTPtr parseMul();

    // 解析一元运算 UnaryExpr → PrimaryExpr ∣ ("+" ∣ "-" ∣ "!") UnaryExpr
    ASTPtr parseUnary();

    // 解析基本表达式 PrimaryExpr → ID ∣ NUMBER ∣ "(" Expr ")" ∣ ID "(" (Expr ("," Expr)∗)? ")"
    ASTPtr parsePrimary();
#pragma endregion
};
