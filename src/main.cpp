// ToyC 编译器主入口
// 支持两种输入：.c/.tc（ToyC 源码）和 .ll（LLVM IR 文本）
// 输出模式：--ast / --ir / --asm / --all（默认输出汇编）

#include "ast.h"
#include "ir.h"
#include "ir_builder.h"
#include "ir_parser.h"
#include "parser.h"
#include "riscv_codegen.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#pragma region 辅助函数
// readFile：读取文件全部内容为字符串
static std::string readFile(const std::string &path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        std::cerr << "Error: Cannot open file '" << path << "'\n";
        exit(1);
    }
    return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

// printUsage：输出命令行帮助信息
static void printUsage(const char *prog) {
    std::cerr << "Usage: " << prog << " <input.[c|tc|ll]> [options]\n"
              << "Options:\n"
              << "  --ast         Print AST\n"
              << "  --ir          Print LLVM IR\n"
              << "  --asm         Print RISC-V assembly\n"
              << "  --all         Print AST + IR + ASM\n"
              << "  -o <file>     Write assembly to file\n";
}
#pragma endregion

#pragma region 主程序
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    // 解析命令行参数
    std::string inputFile = argv[1];
    bool printAst = false, printIr = false, printAsm = false;
    std::string outputFile;

    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ast") == 0)
            printAst = true;
        else if (std::strcmp(argv[i], "--ir") == 0)
            printIr = true;
        else if (std::strcmp(argv[i], "--asm") == 0)
            printAsm = true;
        else if (std::strcmp(argv[i], "--all") == 0) {
            printAst = printIr = printAsm = true;
        } else if (std::strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            outputFile = argv[++i];
    }

    // 默认输出汇编
    if (!printAst && !printIr && !printAsm)
        printAsm = true;

    // 读取输入文件
    std::string source = readFile(inputFile);
    // 判断是否为 .ll（LLVM IR）输入
    bool isLLFile = (inputFile.size() >= 3 && inputFile.substr(inputFile.size() - 3) == ".ll");

    if (isLLFile) {
        // .ll 输入 → 解析为结构化 IR → 代码生成
        toyc::IRParser parser;
        auto mod = parser.parseModule(source);
        if (!mod || mod->functions.empty()) {
            std::cerr << "Error: Failed to parse LLVM IR from '" << inputFile << "'\n";
            return 1;
        }
        if (printIr)
            std::cout << mod->toString();

        std::string asmOutput = toyc::generateRISCVAssembly(*mod);
        if (printAsm)
            std::cout << asmOutput;
        if (!outputFile.empty()) {
            std::ofstream ofs(outputFile);
            ofs << asmOutput;
        }
    } else {
        // .c / .tc 输入 → 词法分析 → 语法分析 → AST → IR → 代码生成
        Parser parser(source);
        auto funcs = parser.parseCompUnit();

        if (printAst) {
            std::cout << "=== AST ===\n";
            for (auto &f : funcs)
                f->print(0, std::cout);
            std::cout << "\n";
        }

        // 仅 --ast 时无需构建 IR / 代码生成，直接返回
        if (!printIr && !printAsm && outputFile.empty())
            return 0;

        // AST → 结构化 IR
        toyc::IRBuilder builder;
        auto mod = builder.buildModule(funcs);

        if (printIr) {
            std::cout << "=== LLVM IR ===\n";
            std::cout << mod->toString();
            std::cout << "\n";
        }

        if (printAsm || !outputFile.empty()) {
            std::string asmOutput = toyc::generateRISCVAssembly(*mod);
            if (printAsm) {
                std::cout << "=== RISC-V Assembly ===\n";
                std::cout << asmOutput;
            }
            if (!outputFile.empty()) {
                std::ofstream ofs(outputFile);
                ofs << asmOutput;
            }
        }
    }

    return 0;
}
#pragma endregion
