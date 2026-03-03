#include "parser.h"

#pragma region 初始化与工具方法
// 构造函数：初始化词法分析器并预读两个 Token 到 cur 和 nxt
Parser::Parser(const std::string &source) : lex(source) {
    cur = lex.nextToken(); // 读取第一个 Token
    nxt = lex.nextToken(); // 读取第二个 Token，实现预读
}

// advance：将 nxt 赋值给 cur，然后从 Lexer 读取下一个 Token 到 nxt
void Parser::advance() {
    cur = nxt;             // 当前 Token 前移
    nxt = lex.nextToken(); // 预读下一个 Token
}

// match：如果 cur 类型匹配期望的 TokenType，则 advance 并返回 true，否则返回 false
bool Parser::match(TokenType t) {
    if (cur.type == t) {
        advance();
        return true;
    }
    return false;
}

// expect：断言 cur 类型为期望值，否则打印错误并退出
void Parser::expect(TokenType t) {
    if (!match(t)) {
        std::cerr << "Unexpected token '" << cur.lexeme << "' at line " << cur.line << "\n";
        exit(1);
    }
}

// parseCompUnit：解析编译单元 CompUnit → FuncDef+，返回所有函数定义
std::vector<std::shared_ptr<FuncDef>> Parser::parseCompUnit() {
    std::vector<std::shared_ptr<FuncDef>> funcs;
    // 只要下一个是 int 或 void，就不断解析函数定义
    while (cur.type == TokenType::INT || cur.type == TokenType::VOID)
        funcs.push_back(parseFuncDef());
    return funcs;
}
#pragma endregion

#pragma region 函数定义解析
// parseFuncDef：解析函数定义 FuncDef → ("int" ∣ "void") ID "(" Params? ")" Block
std::shared_ptr<FuncDef> Parser::parseFuncDef() {
    std::string retType = cur.lexeme; // 保存返回类型
    advance();                        // 消费 int/void

    // 函数名必须为标识符
    if (cur.type != TokenType::ID) {
        std::cerr << "Expected function name, got '" << cur.lexeme << "' at line " << cur.line
                  << "\n";
        exit(1);
    }
    std::string name = cur.lexeme; // 保存函数名
    advance();                     // 消费函数名

    expect(TokenType::LPAREN);   // 消费 '('
    auto params = parseParams(); // 解析参数列表
    expect(TokenType::RPAREN);   // 消费 ')'

    auto body = parseBlock(); // 解析函数体 Block

    // 构造 FuncDef 节点并返回
    auto f = std::make_shared<FuncDef>();
    f->retType = retType;
    f->name = name;
    f->params = params;
    f->body = body;
    return f;
}

// parseParams：解析参数列表 Param → "int" ID ("," "int" ID)*
std::vector<Param> Parser::parseParams() {
    std::vector<Param> ps;
    // 如果当前是 int，则至少有一个参数
    if (cur.type == TokenType::INT) {
        while (true) {
            advance(); // 消费 'int'
            if (cur.type != TokenType::ID) {
                std::cerr << "Expected parameter name after 'int', got '" << cur.lexeme
                          << "' at line " << cur.line << "\n";
                exit(1);
            }
            ps.push_back({cur.lexeme});
            advance();                    // 消费参数名
            if (!match(TokenType::COMMA)) // 若无逗号则退出
                break;
        }
    }
    return ps;
}

// parseBlock：解析语句块 Block → "{" Stmt∗ "}"
// 注意：在 block 层级直接处理 int 声明，避免多声明 (int a=1, b=2;) 被包装成
// 额外的 BlockStmt 而引入多余的作用域，导致变量在退出后丢失
std::shared_ptr<BlockStmt> Parser::parseBlock() {
    expect(TokenType::LBRACE); // 消费 '{'
    auto block = std::make_shared<BlockStmt>();
    // 循环解析直到 '}'
    while (!match(TokenType::RBRACE)) {
        // 直接在 block 层级处理声明语句
        if (cur.type == TokenType::INT) {
            advance(); // 消费 'int'
            do {
                if (cur.type != TokenType::ID) {
                    std::cerr << "Expected identifier after 'int', got '" << cur.lexeme
                              << "' at line " << cur.line << "\n";
                    exit(1);
                }
                std::string name = cur.lexeme;
                advance();                 // 消费变量名
                expect(TokenType::ASSIGN); // 消费 '='
                auto e = parseExpr();      // 解析初始化表达式
                block->stmts.push_back(std::make_shared<DeclStmt>(name, e));
            } while (match(TokenType::COMMA)); // 支持逗号分隔的多变量声明
            expect(TokenType::SEMI);           // 消费 ';'
        } else {
            block->stmts.push_back(parseStmt());
        }
    }
    return block;
}
#pragma endregion

#pragma region 语句解析
// parseStmt：解析单条语句，支持多种语句形式
ASTPtr Parser::parseStmt() {
    // 块语句
    if (cur.type == TokenType::LBRACE)
        return parseBlock();

    // 空语句
    if (match(TokenType::SEMI))
        return nullptr;

    // if 语句
    if (cur.type == TokenType::IF) {
        advance(); // 消费 'if'
        expect(TokenType::LPAREN);
        auto cond = parseExpr(); // 解析条件
        expect(TokenType::RPAREN);
        auto thenS = parseStmt(); // 解析 then 分支
        ASTPtr elseS = nullptr;
        if (match(TokenType::ELSE))
            elseS = parseStmt(); // 解析 else 分支（可选）
        return std::make_shared<IfStmt>(cond, thenS, elseS);
    }

    // while 循环语句
    if (cur.type == TokenType::WHILE) {
        advance(); // 消费 'while'
        expect(TokenType::LPAREN);
        auto cond = parseExpr(); // 解析循环条件
        expect(TokenType::RPAREN);
        auto body = parseStmt(); // 解析循环体
        return std::make_shared<WhileStmt>(cond, body);
    }

    // return 语句
    if (match(TokenType::RETURN)) {
        ASTPtr e = nullptr;
        if (cur.type != TokenType::SEMI)
            e = parseExpr(); // 解析返回值表达式（可选）
        expect(TokenType::SEMI);
        return std::make_shared<ReturnStmt>(e);
    }

    // break 语句
    if (match(TokenType::BREAK)) {
        expect(TokenType::SEMI);
        return std::make_shared<BreakStmt>();
    }

    // continue 语句
    if (match(TokenType::CONTINUE)) {
        expect(TokenType::SEMI);
        return std::make_shared<ContinueStmt>();
    }

    // 变量声明语句：int x = expr, ...;
    if (cur.type == TokenType::INT) {
        advance(); // 消费 'int'
        std::vector<ASTPtr> decls;
        do {
            if (cur.type != TokenType::ID) {
                std::cerr << "Expected identifier after 'int', got '" << cur.lexeme << "' at line "
                          << cur.line << "\n";
                exit(1);
            }
            std::string name = cur.lexeme;
            advance();                 // 消费变量名
            expect(TokenType::ASSIGN); // 消费 '='
            auto e = parseExpr();      // 解析初始化表达式
            decls.push_back(std::make_shared<DeclStmt>(name, e));
        } while (match(TokenType::COMMA)); // 支持逗号分隔的多变量声明
        expect(TokenType::SEMI);
        // 单个声明直接返回，否则包装成 BlockStmt
        if (decls.size() == 1)
            return decls[0];
        auto blk = std::make_shared<BlockStmt>();
        blk->stmts = decls;
        return blk;
    }

    // 区分函数调用和赋值语句（通过预读 nxt 判断）
    if (cur.type == TokenType::ID) {
        std::string name = cur.lexeme;
        if (nxt.type == TokenType::LPAREN) {
            // 函数调用语句
            advance(); // 消费函数名
            advance(); // 消费 '('
            auto call = std::make_shared<CallExpr>(name);
            if (cur.type != TokenType::RPAREN) {
                do {
                    call->args.push_back(parseExpr()); // 解析实参
                } while (match(TokenType::COMMA));
            }
            expect(TokenType::RPAREN);
            expect(TokenType::SEMI);
            return call;
        } else if (nxt.type == TokenType::ASSIGN) {
            // 赋值语句
            advance(); // 消费变量名
            advance(); // 消费 '='
            auto e = parseExpr();
            expect(TokenType::SEMI);
            return std::make_shared<AssignStmt>(name, e);
        }
    }

    // 一般表达式语句 Expr ';'
    {
        auto e = parseExpr();
        expect(TokenType::SEMI);
        return e;
    }
}
#pragma endregion

#pragma region 表达式解析
// parseExpr：入口，解析逻辑或表达式
ASTPtr Parser::parseExpr() { return parseLOr(); }

// parseLOr：解析逻辑或 LOrExpr → LAndExpr ∣ LOrExpr "||" LAndExpr
ASTPtr Parser::parseLOr() {
    auto left = parseLAnd();
    while (match(TokenType::OR)) {
        auto right = parseLAnd();
        left = std::make_shared<BinaryExpr>("||", left, right);
    }
    return left;
}

// parseLAnd：解析逻辑与 LAndExpr → RelExpr ∣ LAndExpr "&&" RelExpr
ASTPtr Parser::parseLAnd() {
    auto left = parseRel();
    while (match(TokenType::AND)) {
        auto right = parseRel();
        left = std::make_shared<BinaryExpr>("&&", left, right);
    }
    return left;
}

// parseRel：解析关系运算 RelExpr → AddExpr ∣ RelExpr ("<"|">"|"<="|">="|"=="|"!=") AddExpr
ASTPtr Parser::parseRel() {
    auto left = parseAdd();
    while (cur.type == TokenType::LT || cur.type == TokenType::GT || cur.type == TokenType::LE ||
           cur.type == TokenType::GE || cur.type == TokenType::EQ || cur.type == TokenType::NE) {
        std::string op = cur.lexeme;
        advance();
        auto right = parseAdd();
        left = std::make_shared<BinaryExpr>(op, left, right);
    }
    return left;
}

// parseAdd：解析加减 AddExpr → MulExpr ∣ AddExpr ("+" ∣ "-") MulExpr
ASTPtr Parser::parseAdd() {
    auto left = parseMul();
    while (cur.type == TokenType::PLUS || cur.type == TokenType::MINUS) {
        std::string op = cur.lexeme;
        advance();
        auto right = parseMul();
        left = std::make_shared<BinaryExpr>(op, left, right);
    }
    return left;
}

// parseMul：解析乘除模 MulExpr → UnaryExpr ∣ MulExpr ("*" ∣ "/" ∣ "%") UnaryExpr
ASTPtr Parser::parseMul() {
    auto left = parseUnary();
    while (cur.type == TokenType::TIMES || cur.type == TokenType::DIV ||
           cur.type == TokenType::MOD) {
        std::string op = cur.lexeme;
        advance();
        auto right = parseUnary();
        left = std::make_shared<BinaryExpr>(op, left, right);
    }
    return left;
}

// parseUnary：解析一元运算 UnaryExpr → PrimaryExpr ∣ ("+" ∣ "-" ∣ "!") UnaryExpr
ASTPtr Parser::parseUnary() {
    if (cur.type == TokenType::PLUS || cur.type == TokenType::MINUS || cur.type == TokenType::NOT) {
        std::string op = cur.lexeme;
        advance();
        auto e = parseUnary(); // 递归解析，支持连续一元运算（如 --x, !!x）
        return std::make_shared<UnaryExpr>(op, e);
    }
    return parsePrimary();
}

// parsePrimary：解析基本表达式 PrimaryExpr → ID ∣ NUMBER ∣ "(" Expr ")" ∣ ID "(" args ")"
ASTPtr Parser::parsePrimary() {
    // 标识符：可能是变量引用或函数调用
    if (cur.type == TokenType::ID) {
        std::string name = cur.lexeme;
        advance();
        if (match(TokenType::LPAREN)) {
            // 函数调用 ID "(" args ")"
            auto call = std::make_shared<CallExpr>(name);
            // 解析参数列表，直到遇到右括号
            while (cur.type != TokenType::RPAREN) {
                call->args.push_back(parseExpr());
                match(TokenType::COMMA);
            }
            expect(TokenType::RPAREN);
            return call;
        }
        // 变量引用
        return std::make_shared<IdentifierExpr>(name);
    }
    // 数字字面量
    if (cur.type == TokenType::NUMBER) {
        int v = stoi(cur.lexeme);
        advance();
        return std::make_shared<NumberExpr>(v);
    }
    // 括号表达式 "(" Expr ")"
    if (match(TokenType::LPAREN)) {
        auto e = parseExpr();
        expect(TokenType::RPAREN);
        return e;
    }
    // 非预期 Token，报错
    std::cerr << "Unexpected primary: " << cur.lexeme << " at line " << cur.line << "\n";
    exit(1);
}
#pragma endregion
