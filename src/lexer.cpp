#include "lexer.h"
#include <unordered_map>

#pragma region 关键字映射表
// 关键字映射表：将标识符字符串映射到对应的 TokenType
static const std::unordered_map<std::string, TokenType> keywords = {
    {"int", TokenType::INT},     {"void", TokenType::VOID},        {"if", TokenType::IF},
    {"else", TokenType::ELSE},   {"while", TokenType::WHILE},      {"return", TokenType::RETURN},
    {"break", TokenType::BREAK}, {"continue", TokenType::CONTINUE}};
#pragma endregion

#pragma region 基础扫描方法
// 构造函数：初始化源代码和行号
Lexer::Lexer(const std::string &source) : src(source), pos(0), line(1) {}

// peek：返回当前字符但不推进位置；若到达末尾，返回 '\0'
char Lexer::peek() const { return (pos >= src.size()) ? '\0' : src[pos]; }

// advance：消费当前字符并返回它，同时将扫描位置 pos 前移一位
char Lexer::advance() {
    char c = peek();
    ++pos;
    return c;
}

// skipWhitespace：跳过空白字符（空格、制表符、换行）并更新行号
void Lexer::skipWhitespace() {
    while (isspace(peek())) {
        if (peek() == '\n')
            ++line; // 行号递增
        advance();  // 消费空白
    }
}

// makeToken：基于最近消费的单字符生成 Token，lexeme 为该字符
Token Lexer::makeToken(TokenType type) { return {type, std::string(1, src[pos - 1]), line}; }
#pragma endregion

#pragma region 词法单元识别
// identifier：解析标识符或关键字，支持字母、数字和下划线
Token Lexer::identifier() {
    size_t start = pos - 1; // 回退一位，因为 nextToken() 中已经 advance() 了一次
    while (isalnum(peek()) || peek() == '_')
        advance();
    std::string lex = src.substr(start, pos - start);
    auto it = keywords.find(lex);
    // 如果找到关键字，则返回关键字类型，否则返回标识符类型(ID)
    TokenType type = (it != keywords.end()) ? it->second : TokenType::ID;
    return {type, lex, line};
}

// number：解析数字字面量，连续读取数字字符
Token Lexer::number() {
    size_t start = pos - 1; // 回退一位，因为 nextToken() 中已经 advance() 了一次
    while (isdigit(peek()))
        advance();
    return {TokenType::NUMBER, src.substr(start, pos - start), line};
}

// nextToken：主入口，跳过空白和注释，识别并返回下一个 Token
Token Lexer::nextToken() {
    // 循环跳过所有空白和注释
    for (;;) {
        skipWhitespace(); // 跳过空白字符
        // 单行注释：遇到 "//" 则跳过至行尾
        if (peek() == '/' && pos + 1 < src.size() && src[pos + 1] == '/') {
            advance(); // 消费 '/'
            advance(); // 消费第二个 '/'
            while (peek() != '\n' && peek() != '\0')
                advance(); // 跳至行尾或文件末尾
            continue;
        }
        // 多行注释：遇到 "/*" 则跳过至 "*/"
        if (peek() == '/' && pos + 1 < src.size() && src[pos + 1] == '*') {
            advance(); // 消费 '/'
            advance(); // 消费 '*'
            while (!(peek() == '*' && pos + 1 < src.size() && src[pos + 1] == '/')) {
                if (peek() == '\n')
                    ++line; // 多行注释中也需要更新行号
                if (peek() == '\0')
                    break; // 文件结束则退出（未闭合注释）
                advance();
            }
            if (peek() != '\0') {
                advance(); // 消费 '*'
                advance(); // 消费 '/'
            }
            continue;
        }
        break; // 既不是空白也不是注释，退出循环
    }

    char c = advance(); // 消费第一个有效字符
    switch (c) {
    case '\0':
        return {TokenType::END, "", line}; // 文件结束

    // 单字符算术运算符
    case '+':
        return makeToken(TokenType::PLUS);
    case '-':
        return makeToken(TokenType::MINUS);
    case '*':
        return makeToken(TokenType::TIMES);
    case '/':
        return makeToken(TokenType::DIV);
    case '%':
        return makeToken(TokenType::MOD);

    // 可能为双字符的运算符
    case '=':
        if (peek() == '=') {
            advance();
            return {TokenType::EQ, "==", line};
        }
        return makeToken(TokenType::ASSIGN);
    case '<':
        if (peek() == '=') {
            advance();
            return {TokenType::LE, "<=", line};
        }
        return makeToken(TokenType::LT);
    case '>':
        if (peek() == '=') {
            advance();
            return {TokenType::GE, ">=", line};
        }
        return makeToken(TokenType::GT);
    case '!':
        if (peek() == '=') {
            advance();
            return {TokenType::NE, "!=", line};
        }
        return makeToken(TokenType::NOT);

    // 双字符逻辑运算符
    case '&':
        if (peek() == '&') {
            advance();
            return {TokenType::AND, "&&", line};
        }
        break;
    case '|':
        if (peek() == '|') {
            advance();
            return {TokenType::OR, "||", line};
        }
        break;

    // 分隔符
    case '(':
        return makeToken(TokenType::LPAREN);
    case ')':
        return makeToken(TokenType::RPAREN);
    case '{':
        return makeToken(TokenType::LBRACE);
    case '}':
        return makeToken(TokenType::RBRACE);
    case ';':
        return makeToken(TokenType::SEMI);
    case ',':
        return makeToken(TokenType::COMMA);

    default:
        if (isalpha(c) || c == '_')
            return identifier(); // 以字母或下划线开头，解析为标识符/关键字
        if (isdigit(c))
            return number(); // 以数字开头，解析为数字字面量
        break;
    }
    // 其他未识别字符
    return {TokenType::UNKNOWN, std::string(1, c), line};
}
#pragma endregion
