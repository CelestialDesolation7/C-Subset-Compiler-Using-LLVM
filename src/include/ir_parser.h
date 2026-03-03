#pragma once
#include "ir.h"
#include <memory>
#include <string>

namespace toyc {

// IRParser 类：将 LLVM IR 文本解析为结构化 ir::Module / ir::Function
// 通过正则表达式逐行匹配 IR 指令，构建结构化的 IR 表示
class IRParser {
#pragma region 公有接口
  public:
    // 解析完整的 LLVM IR 文本为 Module（包含所有函数）
    std::unique_ptr<ir::Module> parseModule(const std::string &irText);

    // 解析单个函数（若 funcName 为空则取第一个函数）
    std::unique_ptr<ir::Function> parseFunction(const std::string &irText,
                                                const std::string &funcName = "");
#pragma endregion

#pragma region 私有解析方法
  private:
    // 从函数定义行和函数体文本构建 Function 对象
    std::unique_ptr<ir::Function> parseFunctionFromDefAndBody(const std::string &defLine,
                                                              const std::string &body);

    // 解析单行 LLVM IR 指令为结构化 Instruction
    ir::Instruction parseInstruction(const std::string &line);

    // 解析函数定义行中的参数列表（提取 %N 形式的虚拟寄存器 ID）
    std::vector<int> parseParameters(const std::string &defLine);

    // 解析操作数字符串（如 "%3" → VReg, "42" → Imm, "true" → BoolLit）
    ir::Operand parseOperand(const std::string &text);
#pragma endregion
};

} // namespace toyc
