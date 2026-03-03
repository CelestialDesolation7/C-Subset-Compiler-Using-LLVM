#pragma once
#include "token.h"

// 词法分析器类：负责将源代码字符串拆分成 Token 流
class Lexer {
#pragma region 公有接口
  public:
    // 构造函数：传入整个源代码文本
    explicit Lexer(const std::string &source);

    // 返回下一个 Token
    Token nextToken();
#pragma endregion

#pragma region 私有状态与方法
  private:
    std::string src; // 源代码文本
    size_t pos = 0;  // 当前扫描位置
    int line = 1;    // 当前行号，从 1 开始

    // 查看当前字符但不消费
    char peek() const;

    // 消费并返回当前字符
    char advance();

    // 跳过空白字符（空格、制表符、换行）
    void skipWhitespace();

    // 生成一个指定类型的 Token
    Token makeToken(TokenType type);

    // 解析标识符或关键字
    Token identifier();

    // 解析数字字面量
    Token number();
#pragma endregion
};
