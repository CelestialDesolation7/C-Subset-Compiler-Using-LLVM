int helper(int a, int b) { return a * b + a - b; }

int compute(int x) {
    int a = x + 1;
    int b = x + 2;
    int c = x + 3;
    int d = a + b;   // d 在 helper 调用后仍然活跃
    int e = b + c;   // e 在 helper 调用后仍然活跃
    int f = a * c;   // f 在 helper 调用前死亡（仅用于 tmp）
    int tmp = f + 1; // tmp 在 helper 调用前死亡

    int g = helper(tmp, d);

    // g 来自 helper 返回值(a0)，寄存器提示使其直接分配到 a0
    int result = g + e + d;
    return result;
}

int branch_test(int x) {
    // 控制流造成活跃区间空洞
    int y = x * 2;
    int z = 0;

    if (x > 5) {
        z = y + x; // y 和 x 均活跃
    } else {
        z = x + 1; // y 不活跃（空洞），x 仍活跃
    }

    // 汇合后 y 再次使用（如果走了 else 分支则实际不影响结果，但编译器需完整处理）
    int w = z + y;
    return w;
}

int chain(int n) {
    // 多次函数调用链，每次返回值提示 a0 → 减少 mv
    int a = helper(n, 1);
    int b = helper(a, 2);
    int c = helper(b, 3);
    return c;
}

int main() {
    int r1 = compute(10);
    int r2 = branch_test(3);
    int r3 = branch_test(8);
    int r4 = chain(2);
    int total = r1 + r2 + r3 + r4;
    return total % 256;
}
