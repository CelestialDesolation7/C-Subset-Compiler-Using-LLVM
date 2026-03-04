// ToyC 寄存器分配调试工具
// 在死循环中读取 LLVM IR 文本，输出完整的寄存器分配调试信息：
//   IR 解析 → 基本块分析 → 活跃性分析 → 活跃区间 → 线性扫描分配结果
// 用法：ra_debug [-o output.txt]
//   交互式输入 IR 文本，以单独一行 "END" 结束一次输入

#include "ir.h"
#include "ir_parser.h"
#include "reg_alloc.h"

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

using namespace toyc;
using namespace toyc::ir;

static std::ostream *g_out = &std::cout;

#pragma region 格式化输出辅助

// ======================== 格式化输出辅助 ========================

static void printSeparator(char ch = '=', int width = 60) {
    *g_out << std::string(width, ch) << "\n";
}

static void printHeader(const std::string &title) {
    *g_out << "\n";
    printSeparator();
    *g_out << title << "\n";
    printSeparator();
}

#pragma endregion

#pragma region 调试信息输出

// ======================== 调试信息输出 ========================

/// 输出解析后的 IR 函数信息（基本块、指令、CFG）
static void dumpFunctionInfo(const Function &func) {
    printHeader("函数: " + func.name + "  (返回类型: " + func.returnType + ")");

    *g_out << "参数 vreg: ";
    for (int v : func.paramVregs)
        *g_out << "%" << v << " ";
    *g_out << "\n";
    *g_out << "最大 vreg ID: " << func.maxVregId << "\n";
    *g_out << "基本块数量: " << func.blocks.size() << "\n";

    for (const auto &block : func.blocks) {
        *g_out << "\n--- 基本块 " << block->name << " (ID: " << block->id << ") ---\n";
        *g_out << "  指令数: " << block->insts.size() << "\n";

        // 后继/前驱
        *g_out << "  后继块: ";
        for (const auto *s : block->succs)
            *g_out << s->name << " ";
        *g_out << "\n";
        *g_out << "  前驱块: ";
        for (const auto *p : block->preds)
            *g_out << p->name << " ";
        *g_out << "\n";

        // 指令详情
        *g_out << "  指令列表:\n";
        for (const auto &inst : block->insts) {
            *g_out << "    [" << inst->index << "] ";
            // 简化打印：defReg = X, useRegs = [...]
            int dr = inst->defReg();
            if (dr >= 0)
                *g_out << "%" << dr << " = ";
            *g_out << opcodeToString(inst->opcode);
            auto uses = inst->useRegs();
            if (!uses.empty()) {
                *g_out << "  uses={";
                for (size_t i = 0; i < uses.size(); ++i) {
                    if (i > 0)
                        *g_out << ", ";
                    *g_out << "%" << uses[i];
                }
                *g_out << "}";
            }
            if (inst->isTerminator())
                *g_out << "  [terminator]";
            *g_out << "\n";
        }
    }
}

/// 输出活跃性分析结果（defSet, useSet, liveIn, liveOut）
static void dumpLivenessInfo(const Function &func) {
    printHeader("活跃性分析结果");

    for (const auto &block : func.blocks) {
        *g_out << "基本块 " << block->name << " (ID: " << block->id << "):\n";

        *g_out << "  defSet: {";
        for (auto it = block->defSet.begin(); it != block->defSet.end(); ++it) {
            if (it != block->defSet.begin())
                *g_out << ", ";
            *g_out << "%" << *it;
        }
        *g_out << "}\n";

        *g_out << "  useSet: {";
        for (auto it = block->useSet.begin(); it != block->useSet.end(); ++it) {
            if (it != block->useSet.begin())
                *g_out << ", ";
            *g_out << "%" << *it;
        }
        *g_out << "}\n";

        *g_out << "  liveIn: {";
        for (auto it = block->liveIn.begin(); it != block->liveIn.end(); ++it) {
            if (it != block->liveIn.begin())
                *g_out << ", ";
            *g_out << "%" << *it;
        }
        *g_out << "}\n";

        *g_out << "  liveOut: {";
        for (auto it = block->liveOut.begin(); it != block->liveOut.end(); ++it) {
            if (it != block->liveOut.begin())
                *g_out << ", ";
            *g_out << "%" << *it;
        }
        *g_out << "}\n\n";
    }
}

/// 输出分配结果
static void dumpAllocationResult(const AllocationResult &result, const RegInfo &regInfo) {
    printHeader("分配结果");

    int physCount = 0, spillCount = 0;
    for (const auto &[vreg, phys] : result.vregToPhys) {
        if (phys >= 0)
            physCount++;
    }
    spillCount = static_cast<int>(result.vregToStack.size());

    *g_out << "寄存器映射数: " << result.vregToPhys.size() << "\n";
    *g_out << "  分配到物理寄存器: " << physCount << "\n";
    *g_out << "  溢出到栈: " << spillCount << "\n";

    // 物理寄存器映射
    *g_out << "\n--- vreg → 物理寄存器 ---\n";
    // 收集并排序以便稳定输出
    std::map<int, int> sortedPhys(result.vregToPhys.begin(), result.vregToPhys.end());
    for (const auto &[vreg, phys] : sortedPhys) {
        if (phys >= 0) {
            *g_out << "  %" << vreg << " → " << regInfo.getRegName(phys) << "  (x" << phys << ")\n";
        }
    }

    // 栈溢出映射
    if (!result.vregToStack.empty()) {
        *g_out << "\n--- vreg → 栈偏移 ---\n";
        std::map<int, int> sortedStack(result.vregToStack.begin(), result.vregToStack.end());
        for (const auto &[vreg, slot] : sortedStack) {
            *g_out << "  %" << vreg << " → slot " << slot;
            if (slot > 0)
                *g_out << "  (栈传入参数, s0+" << (slot - 4) << ")";
            else
                *g_out << "  (溢出槽)";
            *g_out << "\n";
        }
    }

    // 参数位置
    if (!result.paramVregToLocation.empty()) {
        *g_out << "\n--- 参数位置 ---\n";
        std::map<int, int> sortedParam(result.paramVregToLocation.begin(),
                                       result.paramVregToLocation.end());
        for (const auto &[vreg, loc] : sortedParam) {
            *g_out << "  %" << vreg << " → ";
            if (loc >= 10 && loc <= 17)
                *g_out << regInfo.getRegName(loc) << "  (寄存器传参)\n";
            else
                *g_out << "栈偏移 " << loc << "  (栈传参)\n";
        }
    }

    // 使用过的物理寄存器
    *g_out << "\n--- 使用过的物理寄存器 ---\n  ";
    for (int r : result.usedPhysRegs)
        *g_out << regInfo.getRegName(r) << " ";
    *g_out << "\n";

    // callee-saved
    if (!result.calleeSavedRegs.empty()) {
        *g_out << "\n--- 需保存的 callee-saved 寄存器 ---\n  ";
        for (int r : result.calleeSavedRegs)
            *g_out << regInfo.getRegName(r) << " ";
        *g_out << "\n";
    }
}

#pragma endregion

#pragma region IR 处理流程

/// 处理一段 LLVM IR 文本：解析 → 分析 → 分配 → 输出
static void processIR(const std::string &irText) {
    // 1. 解析 IR
    IRParser parser;
    auto mod = parser.parseModule(irText);
    if (!mod || mod->functions.empty()) {
        *g_out << "[错误] 无法解析 LLVM IR，请检查输入格式。\n";
        return;
    }

    *g_out << "===============\n";
    *g_out << "\n=== 解析完成 ===\n";
    *g_out << "===============\n";
    *g_out << "\n输入的 LLVM IR:\n" << irText << "\n";

    RegInfo regInfo;

    for (auto &func : mod->functions) {
        // 2. 创建分配器并运行（debugMode 开启会输出活跃区间）
        LinearScanAllocator allocator(regInfo);
        allocator.setDebugMode(true);
        allocator.setDebugOutput(g_out);

        auto result = allocator.allocate(*func);

        // 3. 输出函数结构（在 allocate 之后，指令编号已赋值）
        dumpFunctionInfo(*func);

        // 4. 输出活跃性分析结果（allocate 已经填充了 liveIn/liveOut）
        dumpLivenessInfo(*func);

        // 5. 输出分配结果
        dumpAllocationResult(result, regInfo);
    }

    printSeparator('=', 60);
    *g_out << "分析完成\n";
}
#pragma endregion

#pragma region 主程序
// ======================== 主程序 ========================

int main(int argc, char *argv[]) {
    std::ofstream outputFile;

    // 解析 -o 输出重定向
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            outputFile.open(argv[++i]);
            if (outputFile.is_open())
                g_out = &outputFile;
            else
                std::cerr << "警告: 无法打开输出文件，使用 stdout\n";
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "用法: ra_debug [-o output.txt]\n"
                      << "交互式输入 LLVM IR 文本，以单独一行 \"END\" 结束一次输入。\n"
                      << "输入 \"quit\" 或 \"exit\" 退出。\n";
            return 0;
        }
    }

    std::cout << "=== ToyC 寄存器分配调试工具 ===\n";
    std::cout << "输入 LLVM IR 文本，以单独一行 \"END\" 结束。\n";
    std::cout << "输入 \"quit\" 或 \"exit\" 退出。\n";
    std::cout << "可用命令：\n";
    std::cout << "  CLEAR/cls - 清空屏幕并显示当前缓冲区\n";
    std::cout << "  UNDO      - 撤销上一行输入\n";
    std::cout << "  RESET     - 清空当前缓冲区内容\n";
    std::cout << "  SHOW      - 显示当前缓冲区内容\n\n";

    // 死循环：持续读取 IR 并分析
    while (true) {
        std::cout << ">>> 请输入 LLVM IR (END 结束本次输入):\n";

        std::string line;
        std::vector<std::string> bufferLines;
        bool gotInput = false;

        while (std::getline(std::cin, line)) {
            // 命令处理
            if (line == "END") {
                gotInput = true;
                break;
            }
            if (line == "quit" || line == "exit")
                return 0;

            if (line == "CLEAR" || line == "cls") {
#ifdef _WIN32
                system("cls");
#else
                system("clear");
#endif
                std::cout << "=== ToyC 寄存器分配调试工具 ===\n";
                // 重新显示当前缓冲区
                if (!bufferLines.empty()) {
                    std::cout << ">>> 当前缓冲区内容:\n";
                    for (const auto &l : bufferLines)
                        std::cout << l << "\n";
                }
                std::cout << ">>> 请继续输入 (END 结束):\n";
                continue;
            }

            if (line == "UNDO") {
                if (!bufferLines.empty()) {
                    bufferLines.pop_back();
                    std::cout << "[INFO] 已撤销上一行。当前行数: " << bufferLines.size() << "\n";
                } else {
                    std::cout << "[INFO] 缓冲区为空，无法撤销。\n";
                }
                continue;
            }

            if (line == "RESET") {
                bufferLines.clear();
                std::cout << "[INFO] 缓冲区已清空。\n";
                continue;
            }

            if (line == "SHOW") {
                std::cout << ">>> 当前缓冲区内容 (" << bufferLines.size() << " 行):\n";
                for (size_t i = 0; i < bufferLines.size(); ++i) {
                    std::cout << "[" << i + 1 << "] " << bufferLines[i] << "\n";
                }
                continue;
            }

            // 普通 IR 文本
            bufferLines.push_back(line);
        }

        // 构建 irText
        std::string irText;
        for (const auto &l : bufferLines)
            irText += l + "\n";

        // stdin EOF → 退出
        if (!gotInput && irText.empty())
            break;

        if (irText.empty()) {
            std::cout << "[提示] 未输入有效内容，请重试。\n\n";
            continue;
        }

        try {
            processIR(irText);
        } catch (const std::exception &e) {
            *g_out << "[异常] " << e.what() << "\n";
        }

        *g_out << "\n";
    }

    if (outputFile.is_open())
        outputFile.close();

    return 0;
}

#pragma endregion
