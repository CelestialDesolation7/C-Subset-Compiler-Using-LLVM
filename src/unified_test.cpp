// ToyC 统一测试程序
// 遍历测试目录下所有 .c 文件，依次执行完整编译流水线：
//   解析 → AST → IR 用生成 → IR round-trip → 寄存器分配 → 代码生成
// 任一阶段失败则报告 FAIL，全部通过则返回 0

#include "ast.h"
#include "ir.h"
#include "ir_builder.h"
#include "ir_parser.h"
#include "parser.h"
#include "reg_alloc.h"
#include "riscv_codegen.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

#pragma region 辅助函数

// readFile：读取文件全部内容
static std::string readFile(const std::string &path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        std::cerr << "  [ERROR] Cannot open: " << path << "\n";
        return "";
    }
    return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

#pragma endregion

#pragma region 单文件测试

/**
 * @brief 测试单个 .c 文件的完整编译流水线
 * @param path    测试文件路径
 * @param verbose 是否输出详细的 IR/ASM
 * @return true 表示测试通过
 * @details 流程：解析 → IR 生成 → IR 重新解析（round-trip 验证）→ 寄存器分配 → 代码生成
 */
static bool testFile(const std::string &path, bool verbose) {
    std::string source = readFile(path);
    if (source.empty())
        return false;

    std::string filename = fs::path(path).filename().string();
    std::cout << "Testing: " << filename << " ... ";

    try {
        // 1. 词法分析 + 语法分析
        Parser parser(source);
        auto funcs = parser.parseCompUnit();
        if (funcs.empty()) {
            std::cout << "FAIL (no functions parsed)\n";
            return false;
        }

        // 2. AST → 结构化 IR
        toyc::IRBuilder builder;
        auto mod = builder.buildModule(funcs);
        std::string irText = mod->toString();
        if (irText.empty()) {
            std::cout << "FAIL (empty IR)\n";
            return false;
        }

        if (verbose) {
            std::cout << "\n--- IR ---\n" << irText << "\n";
        }

        // 3. IR → 文本 → 重新解析（验证 IR 的 round-trip 一致性）
        toyc::IRParser irParser;
        auto reparsed = irParser.parseModule(irText);
        if (!reparsed || reparsed->functions.empty()) {
            std::cout << "FAIL (IR re-parse failed)\n";
            return false;
        }

        // 4. 寄存器分配（验证分配器不崩溃）
        toyc::RegInfo regInfo;
        for (auto &func : mod->functions) {
            toyc::LinearScanAllocator allocator(regInfo);
            auto result = allocator.allocate(*func);
        }

        // 5. RISC-V 代码生成
        std::string asmOutput = toyc::generateRISCVAssembly(*mod);
        if (asmOutput.empty()) {
            std::cout << "FAIL (empty assembly)\n";
            return false;
        }

        if (verbose) {
            std::cout << "--- ASM ---\n" << asmOutput << "\n";
        }

        std::cout << "OK\n";
        return true;
    } catch (const std::exception &e) {
        std::cout << "FAIL (" << e.what() << ")\n";
        return false;
    }
}

#pragma endregion

#pragma region 主程序

int main(int argc, char *argv[]) {
    // 解析命令行参数
    bool verbose = false;
    std::string testDir = "examples/compiler_inputs";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-v" || arg == "--verbose")
            verbose = true;
        else
            testDir = arg;
    }

    if (!fs::exists(testDir)) {
        std::cerr << "Test directory not found: " << testDir << "\n";
        return 1;
    }

    std::cout << "=== ToyC Unified Test ===\n";
    std::cout << "Test directory: " << testDir << "\n\n";

    // 收集并排序测试文件
    int total = 0, passed = 0;
    std::vector<std::string> files;

    for (auto &entry : fs::directory_iterator(testDir)) {
        if (entry.path().extension() == ".c")
            files.push_back(entry.path().string());
    }
    std::sort(files.begin(), files.end());

    for (auto &file : files) {
        total++;
        if (testFile(file, verbose))
            passed++;
    }

    std::cout << "\n=== Results: " << passed << "/" << total << " passed ===\n";
    return (passed == total) ? 0 : 1;
}

#pragma endregion
