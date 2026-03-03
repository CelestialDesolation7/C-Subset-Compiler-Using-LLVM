#pragma once
#include <string>

#pragma region TokenType 枚举
// TokenType 枚举：表示词法分析器输出的各种符号类型
enum class TokenType {
    // 字面量与标识符
    ID,     // 标识符
    NUMBER, // 数字字面量

    // 关键字
    INT,      // int 关键字
    VOID,     // void 关键字
    IF,       // if 关键字
    ELSE,     // else 关键字
    WHILE,    // while 关键字
    RETURN,   // return 关键字
    BREAK,    // break 关键字
    CONTINUE, // continue 关键字

    // 算术运算符
    PLUS,  // '+'
    MINUS, // '-'
    TIMES, // '*'
    DIV,   // '/'
    MOD,   // '%'

    // 关系运算符
    GT, // '>'
    LT, // '<'
    GE, // '>='
    LE, // '<='
    EQ, // '=='
    NE, // '!='

    // 逻辑运算符
    OR,  // '||'
    AND, // '&&'
    NOT, // '!' (一元逻辑非)

    // 赋值运算符
    ASSIGN, // '='

    // 分隔符
    LPAREN, // '('
    RPAREN, // ')'
    LBRACE, // '{'
    RBRACE, // '}'
    SEMI,   // ';'
    COMMA,  // ','

    // 文件结束与未知
    END,    // 文件结束标记
    UNKNOWN // 未知或非法 Token
};
#pragma endregion

#pragma region Token 结构体
// Token 结构体：表示词法单元
struct Token {
    TokenType type;     // Token 类型
    std::string lexeme; // 原始文本内容
    int line;           // 所在行号，用于错误定位
};
#pragma endregion
