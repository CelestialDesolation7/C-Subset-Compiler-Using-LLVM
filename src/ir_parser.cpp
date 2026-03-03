#include "ir_parser.h"
#include <algorithm>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace toyc {

using namespace ir;

#pragma region 辅助函数

// trim：去除字符串前后的空白字符
static std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// startsWith：判断字符串是否以指定前缀开头
static bool startsWith(const std::string &s, const std::string &prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

#pragma endregion

#pragma region 模块解析

// parseModule：解析完整的 LLVM IR 文本为 Module
// 遍历每一行，匹配 "define" 开头的函数定义，收集函数体直到 "}"
std::unique_ptr<Module> IRParser::parseModule(const std::string &irText) {
    auto mod = std::make_unique<Module>();
    std::istringstream iss(irText);
    std::string line;

    // 收集函数文本
    std::vector<std::pair<std::string, std::string>> funcTexts; // (defLine, body)
    bool inFunc = false;
    std::string defLine;
    std::string body;

    while (std::getline(iss, line)) {
        std::string trimmed = trim(line);
        if (startsWith(trimmed, "define ")) {
            inFunc = true;
            defLine = trimmed;
            body.clear();
            continue;
        }
        if (inFunc) {
            if (trimmed == "}") {
                funcTexts.push_back({defLine, body});
                inFunc = false;
            } else {
                body += line + "\n";
            }
        }
    }

    for (auto &[def, funcBody] : funcTexts) {
        auto func = parseFunctionFromDefAndBody(def, funcBody);
        if (func)
            mod->functions.push_back(std::move(func));
    }

    return mod;
}

#pragma endregion

#pragma region 函数解析

// parseFunction：解析指定名称的函数，若 funcName 为空则返回第一个函数
std::unique_ptr<Function> IRParser::parseFunction(const std::string &irText,
                                                  const std::string &funcName) {
    auto mod = parseModule(irText);
    if (mod->functions.empty())
        return nullptr;
    if (funcName.empty())
        return std::move(mod->functions[0]);
    for (auto &f : mod->functions) {
        if (f->name == funcName)
            return std::move(f);
    }
    return nullptr;
}

#pragma endregion

#pragma region 内部辅助：从定义行和函数体构建 Function

// parseFunctionFromDefAndBody：从函数定义行和函数体文本构建 Function 对象
// 1. 用正则解析函数名、返回类型、参数列表
// 2. 创建入口基本块，逐行解析标签和指令
// 3. 跟踪最大虚拟寄存器 ID
std::unique_ptr<Function> IRParser::parseFunctionFromDefAndBody(const std::string &defLine,
                                                                const std::string &body) {

    auto func = std::make_unique<Function>();

    // 解析函数名
    std::regex nameRe("@(\\w+)");
    std::smatch m;
    if (std::regex_search(defLine, m, nameRe))
        func->name = m[1].str();

    // 解析返回类型
    if (defLine.find("void") != std::string::npos && defLine.find("void") < defLine.find("@"))
        func->returnType = "void";
    else
        func->returnType = "int";

    // 解析参数
    func->paramVregs = parseParameters(defLine);
    for (size_t i = 0; i < func->paramVregs.size(); ++i)
        func->params.push_back({std::to_string(func->paramVregs[i]), "i32"});

    // 创建入口基本块
    auto entryBB = std::make_unique<BasicBlock>();
    entryBB->id = 0;
    entryBB->name = "entry";
    func->blockMap["entry"] = entryBB.get();

    BasicBlock *currentBB = entryBB.get();
    func->blocks.push_back(std::move(entryBB));

    // 逐行解析
    std::istringstream iss(body);
    std::string line;
    int maxVreg = -1;

    // 先更新 maxVreg 用参数
    for (auto v : func->paramVregs)
        if (v > maxVreg)
            maxVreg = v;

    while (std::getline(iss, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || startsWith(trimmed, ";"))
            continue;

        // 标签行
        if (trimmed.back() == ':') {
            std::string label = trimmed.substr(0, trimmed.size() - 1);
            label = trim(label);

            auto bb = std::make_unique<BasicBlock>();
            bb->id = static_cast<int>(func->blocks.size());
            bb->name = label;
            func->blockMap[label] = bb.get();
            currentBB = bb.get();
            func->blocks.push_back(std::move(bb));
            continue;
        }

        // 指令行
        Instruction inst = parseInstruction(trimmed);
        int dr = inst.defReg();
        if (dr > maxVreg)
            maxVreg = dr;
        for (int u : inst.useRegs())
            if (u > maxVreg)
                maxVreg = u;

        inst.blockId = currentBB->id;
        currentBB->insts.push_back(std::make_unique<Instruction>(std::move(inst)));
    }

    func->maxVregId = maxVreg;
    return func;
}

#pragma endregion

#pragma region 参数解析

// parseParameters：从函数定义行提取参数虚拟寄存器 ID
// 匹配括号内的 %数字 模式
std::vector<int> IRParser::parseParameters(const std::string &defLine) {
    std::vector<int> paramVregs;
    // 找到 ( ... ) 部分
    auto lp = defLine.find('(');
    auto rp = defLine.find(')');
    if (lp == std::string::npos || rp == std::string::npos)
        return paramVregs;

    std::string paramStr = defLine.substr(lp + 1, rp - lp - 1);
    // 匹配 %数字
    std::regex paramRe("%(\\d+)");
    auto begin = std::sregex_iterator(paramStr.begin(), paramStr.end(), paramRe);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        paramVregs.push_back(std::stoi((*it)[1].str()));
    }
    return paramVregs;
}

#pragma endregion

#pragma region 指令解析

// parseInstruction：解析单行 LLVM IR 指令为结构化 Instruction
// 按优先级尝试匹配：ret → br → store → %def = ... （alloca/load/call/icmp/算术）
Instruction IRParser::parseInstruction(const std::string &line) {
    std::string s = trim(line);

    // ret void
    if (s == "ret void")
        return Instruction::makeRetVoid();

    // ret i32 %X / ret i32 N
    if (startsWith(s, "ret ")) {
        // ret type value
        std::regex retRe(R"(ret\s+(\w+)\s+(.+))");
        std::smatch m;
        if (std::regex_match(s, m, retRe)) {
            return Instruction::makeRet(m[1].str(), parseOperand(trim(m[2].str())));
        }
        return Instruction::makeRetVoid();
    }

    // br label %target
    if (startsWith(s, "br label ")) {
        std::regex brRe(R"(br\s+label\s+%(\S+))");
        std::smatch m;
        if (std::regex_match(s, m, brRe))
            return Instruction::makeBr(Operand::label(m[1].str()));
    }

    // br i1 %cond, label %true, label %false
    if (startsWith(s, "br i1 ")) {
        std::regex condBrRe(R"(br\s+i1\s+(%\d+|true|false),\s*label\s+%(\S+),\s*label\s+%(\S+))");
        std::smatch m;
        if (std::regex_match(s, m, condBrRe)) {
            return Instruction::makeCondBr(parseOperand(m[1].str()), Operand::label(m[2].str()),
                                           Operand::label(m[3].str()));
        }
    }

    // store type value, ptr %ptr, align N
    if (startsWith(s, "store ")) {
        std::regex storeRe(
            R"(store\s+(\w+)\s+(%\d+|\-?\d+|true|false),\s*ptr\s+(%\d+)(?:,\s*align\s+(\d+))?)");
        std::smatch m;
        if (std::regex_match(s, m, storeRe)) {
            int align = m[4].matched ? std::stoi(m[4].str()) : 4;
            return Instruction::makeStore(m[1].str(), parseOperand(m[2].str()),
                                          parseOperand(m[3].str()), align);
        }
    }

    // %def = ...
    std::regex defRe(R"((%\d+)\s*=\s*(.*))");
    std::smatch dm;
    if (std::regex_match(s, dm, defRe)) {
        Operand defOp = parseOperand(dm[1].str());
        std::string rhs = trim(dm[2].str());

        // alloca type, align N
        if (startsWith(rhs, "alloca ")) {
            std::regex allocaRe(R"(alloca\s+(\w+)(?:,\s*align\s+(\d+))?)");
            std::smatch m;
            if (std::regex_match(rhs, m, allocaRe)) {
                int align = m[2].matched ? std::stoi(m[2].str()) : 4;
                return Instruction::makeAlloca(defOp, m[1].str(), align);
            }
        }

        // load type, ptr %ptr, align N
        if (startsWith(rhs, "load ")) {
            std::regex loadRe(R"(load\s+(\w+),\s*ptr\s+(%\d+)(?:,\s*align\s+(\d+))?)");
            std::smatch m;
            if (std::regex_match(rhs, m, loadRe)) {
                int align = m[3].matched ? std::stoi(m[3].str()) : 4;
                return Instruction::makeLoad(defOp, m[1].str(), parseOperand(m[2].str()), align);
            }
        }

        // call type @func(args...)
        if (startsWith(rhs, "call ")) {
            std::regex callRe(R"(call\s+(\w+)\s+@(\w+)\((.*)\))");
            std::smatch m;
            if (std::regex_match(rhs, m, callRe)) {
                std::string retType = m[1].str();
                std::string callee = m[2].str();
                std::string argStr = m[3].str();
                std::vector<Operand> args;
                if (!argStr.empty()) {
                    // 逐个拆分参数 "i32 noundef %X"
                    std::regex argRe(R"((?:i32\s+(?:noundef\s+)?)(%\d+|\-?\d+))");
                    auto begin = std::sregex_iterator(argStr.begin(), argStr.end(), argRe);
                    auto end = std::sregex_iterator();
                    for (auto it = begin; it != end; ++it) {
                        args.push_back(parseOperand((*it)[1].str()));
                    }
                }
                return Instruction::makeCall(defOp, retType, callee, std::move(args));
            }
        }

        // icmp pred type lhs, rhs
        if (startsWith(rhs, "icmp ")) {
            std::regex icmpRe(R"(icmp\s+(\w+)\s+(\w+)\s+(%\d+|\-?\d+),\s*(%\d+|\-?\d+))");
            std::smatch m;
            if (std::regex_match(rhs, m, icmpRe)) {
                CmpPred pred = stringToCmpPred(m[1].str());
                return Instruction::makeICmp(pred, defOp, m[2].str(), parseOperand(m[3].str()),
                                             parseOperand(m[4].str()));
            }
        }

        // 算术运算: add/sub/mul/sdiv/srem [nsw] type lhs, rhs
        std::regex arithRe(
            R"((add|sub|mul|sdiv|srem)\s+(?:nsw\s+)?(\w+)\s+(%\d+|\-?\d+),\s*(%\d+|\-?\d+))");
        std::smatch am;
        if (std::regex_match(rhs, am, arithRe)) {
            Opcode opc = stringToArithOpcode(am[1].str());
            return Instruction::makeBinOp(opc, defOp, am[2].str(), parseOperand(am[3].str()),
                                          parseOperand(am[4].str()));
        }
    }

    // 无法识别的指令行 — 返回空 ret void 占位
    return Instruction::makeRetVoid();
}

#pragma endregion

#pragma region 操作数解析

// parseOperand：解析操作数字符串
// "%N" → VReg，"%name" → Label，数字 → Imm，"true"/"false" → BoolLit
Operand IRParser::parseOperand(const std::string &text) {
    std::string s = trim(text);
    if (s.empty())
        return Operand::none();

    if (s == "true")
        return Operand::boolLit(true);
    if (s == "false")
        return Operand::boolLit(false);

    if (s[0] == '%') {
        std::string numStr = s.substr(1);
        // 如果全是数字，就是虚拟寄存器
        bool allDigit = !numStr.empty() && std::all_of(numStr.begin(), numStr.end(), ::isdigit);
        if (allDigit)
            return Operand::vreg(std::stoi(numStr));
        // 否则是标签
        return Operand::label(numStr);
    }

    // 尝试解析为整型常量
    try {
        return Operand::imm(std::stoi(s));
    } catch (...) {
    }

    return Operand::none();
}

#pragma endregion

} // namespace toyc
