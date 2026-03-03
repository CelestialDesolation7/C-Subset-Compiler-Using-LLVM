#pragma once
#include "ir.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace toyc {

#pragma region 活跃区间

// LiveRange：一段连续的活跃范围 [start, end]
struct LiveRange {
    int start, end; // 范围的起止位置（线性化编号）
    LiveRange(int s, int e) : start(s), end(e) {}
    bool operator<(const LiveRange &o) const { return start < o.start; }
    // 判断两个范围是否重叠
    bool overlaps(const LiveRange &o) const { return !(end < o.start || o.end < start); }
    // 判断两个范围是否相邻（可合并）
    bool adjacent(const LiveRange &o) const { return end + 1 == o.start || o.end + 1 == start; }
};

// LiveInterval：虚拟寄存器的活跃区间，由多个 LiveRange 合并而成
class LiveInterval {
  public:
    int vreg = -1;                 // 对应的虚拟寄存器 ID
    std::vector<LiveRange> ranges; // 排序后的活跃范围列表
    int spillSlot = -1;            // 溢出栈槽偏移，-1 表示未溢出
    int physReg = -1;              // 分配到的物理寄存器 ID，-1 表示未分配

    LiveInterval() = default;
    explicit LiveInterval(int v) : vreg(v) {}

    // 添加活跃范围并自动合并重叠/相邻区间
    void addRange(int start, int end);
    // 检查指定位置是否在活跃区间内
    bool contains(int pos) const;
    // 返回最早的活跃起始位置
    int start() const;
    // 返回最晚的活跃结束位置
    int end() const;
    bool empty() const { return ranges.empty(); }

    bool operator<(const LiveInterval &o) const { return start() < o.start(); }
};

#pragma endregion

#pragma region 物理寄存器

// PhysReg：物理寄存器描述
struct PhysReg {
    int id = 0;               // 寄存器编号（x0-x31）
    std::string name;         // 寄存器名称（如 "a0", "s1", "t2"）
    bool callerSaved = false; // 是否为调用者保存寄存器
    bool calleeSaved = false; // 是否为被调用者保存寄存器
    bool reserved = false;    // 是否为保留寄存器（不参与分配）
    int priority = 0;         // 分配优先级，值越小越优先选用

    PhysReg() = default;
    PhysReg(int i, const std::string &n, bool caller, bool callee, bool res, int prio)
        : id(i), name(n), callerSaved(caller), calleeSaved(callee), reserved(res), priority(prio) {}
};

// PhysRegComparator：按优先级排序物理寄存器（优先级相同时按 ID）
struct PhysRegComparator {
    const std::vector<PhysReg> *physRegs;
    explicit PhysRegComparator(const std::vector<PhysReg> *regs) : physRegs(regs) {}
    bool operator()(int a, int b) const;
};

// RegInfo：目标架构（RV32I）物理寄存器信息
// 描述 x0-x31 共 32 个寄存器的属性及可分配集合
class RegInfo {
  public:
    std::vector<PhysReg> physRegs;                    // 32 个物理寄存器描述
    std::set<int, PhysRegComparator> allocatableRegs; // 可参与分配的寄存器集合

    // 构造函数：初始化 RV32I 寄存器描述
    RegInfo();

    bool isReserved(int id) const;    // 是否为保留寄存器
    bool isCallerSaved(int id) const; // 是否为调用者保存
    bool isCalleeSaved(int id) const; // 是否为被调用者保存
    const PhysReg &getReg(int id) const { return physRegs[id]; }
    std::string getRegName(int id) const; // 获取寄存器名称
};

#pragma endregion

#pragma region 活跃性分析

// LivenessAnalysis：基于数据流方程的活跃性分析
// 计算每个基本块的 useSet/defSet/liveIn/liveOut
class LivenessAnalysis {
  public:
    // 执行完整的活跃性分析流程（构建 CFG → 计算 use/def → 迭代求解）
    void run(ir::Function &F);
    // 从入口块开始构建逆后序（RPO）遍历序列
    static std::vector<ir::BasicBlock *> buildRPO(ir::BasicBlock *entry);

  private:
    // 扫描所有指令，计算每个基本块的 useSet（先使用后定义）和 defSet
    void computeUseDefSets(ir::Function &F);
    // 反向迭代求解 liveIn/liveOut 直到不动点
    void computeLivenessIteratively(ir::Function &F);
};

// LiveIntervalBuilder：根据活跃性分析结果，为每个虚拟寄存器构建活跃区间
class LiveIntervalBuilder {
  public:
    /**
     * @param F         目标函数 IR
     * @param LA        已完成的活跃性分析
     * @param splitting 是否使用简化区间模式
     */
    LiveIntervalBuilder(ir::Function &F, const LivenessAnalysis &LA, bool splitting = false);
    // 构建所有虚拟寄存器的活跃区间映射
    std::unordered_map<int, std::unique_ptr<LiveInterval>> build();

  private:
    ir::Function &F_;
    const LivenessAnalysis &LA_;
    bool splitting_;

    // 为单个 vreg 构建精确的活跃区间（考虑跨基本块的活跃传播）
    void buildIntervalForVreg(int vreg, std::unique_ptr<LiveInterval> &interval);
    // 为单个 vreg 构建简化区间（仅在 def/use 点各加一个点区间）
    void buildSimplifiedIntervalForVreg(int vreg, std::unique_ptr<LiveInterval> &interval);
};

#pragma endregion

#pragma region 分配结果

// AllocationResult：寄存器分配的最终输出
class AllocationResult {
  public:
    std::unordered_map<int, int> vregToPhys;  // vreg → 物理寄存器 ID（-1 = 已溢出）
    std::unordered_map<int, int> vregToStack; // vreg → 栈字节偏移（仅溢出的 vreg）
    std::unordered_map<int, int> paramVregToLocation; // 参数 vreg → 位置（寄存器 ID 或栈偏移）
    std::set<int> usedPhysRegs;                       // 实际使用过的物理寄存器集合
    std::set<int> calleeSavedRegs; // 使用过的被调用者保存寄存器（需在函数入口/出口保护）
};

#pragma endregion

#pragma region 线性扫描分配器

// LinearScanAllocator：基于活跃区间的线性扫描寄存器分配器
// 核心流程：
//   1. 处理函数参数（绑定 a0-a7 或栈）
//   2. 活跃性分析 + 指令编号
//   3. 构建活跃区间
//   4. 按起始位置排序，线性扫描分配物理寄存器
//   5. 无空闲寄存器时，溢出结束最晚的区间
class LinearScanAllocator {
  public:
    explicit LinearScanAllocator(const RegInfo &regInfo);

    /**
     * @brief 对函数执行寄存器分配
     * @param F 目标函数 IR
     * @return 分配结果（vreg→physReg / vreg→stack 映射）
     */
    AllocationResult allocate(ir::Function &F);

    void setDebugMode(bool enable) { debugMode_ = enable; }
    void setDebugOutput(std::ostream *os) { debugOutput_ = os; }

    const AllocationResult &getAllocationResult() const { return result_; }
    // 获取实际使用过的物理寄存器集合
    std::set<int> getUsedPhysRegs() const;
    // 获取使用过的被调用者保存寄存器集合
    std::set<int> getCalleeSavedRegs() const;

    // 分配溢出临时寄存器（t0/t1 交替使用）
    int allocateSpillTempReg();
    // 判断是否为溢出临时寄存器
    bool isSpillTempReg(int regId) const;

    // 调试输出：打印所有活跃区间
    void dumpIntervals(const std::unordered_map<int, std::unique_ptr<LiveInterval>> &intervals);

  private:
    const RegInfo &regInfo_;                 // 目标架构寄存器信息
    bool debugMode_ = false;                 // 调试模式开关
    std::ostream *debugOutput_ = &std::cout; // 调试输出流

    std::vector<bool> isPhysRegUsed_;               // 物理寄存器使用标记（32 位）
    std::set<int, PhysRegComparator> freePhysRegs_; // 当前空闲的物理寄存器集合

    int spillTempReg1_ = 5, spillTempReg2_ = 6; // 溢出临时寄存器 ID（t0, t1）
    bool spillTempCounter_ = false;             // 交替选择计数器
    std::set<int> allocatedVregs_;              // 已分配的虚拟寄存器集合

    std::vector<LiveInterval *> active_; // 当前活跃的区间列表（按结束位置排序）
    AllocationResult result_;            // 分配结果
    int nextSpillSlot_ = 0;              // 下一个溢出槽编号

    // -------- 参数处理 --------
    // 将参数 vreg 绑定到 a0-a7 或栈位置
    void processParameters(const std::vector<int> &paramVregs);

    // -------- 指令编号 --------
    // 为所有指令按 RPO 顺序分配线性位置编号
    void assignInstrPositions(ir::Function &F);

    // -------- 线性扫描核心 --------
    // 执行线性扫描分配算法
    AllocationResult
    runLinearScan(const std::unordered_map<int, std::unique_ptr<LiveInterval>> &intervals);

    // 过期回收：释放在 curStart 之前已经结束的活跃区间占用的物理寄存器
    void expireOldIntervals(int curStart);
    // 为区间分配一个空闲的物理寄存器
    void allocatePhysicalReg(LiveInterval &interval);
    // 溢出处理：将当前区间或 active 中结束最晚的区间溢出到栈
    void spillAtInterval(LiveInterval &interval);
    // 分配一个新的栈溢出槽（每次 -4 字节）
    int allocateSpillSlot();

    // -------- 物理寄存器管理 --------
    // 初始化空闲寄存器池
    void initializeFreeRegs();
    // 从空闲池中分配一个优先级最高的寄存器
    int allocatePhysReg();
    // 将物理寄存器归还到空闲池
    void freePhysReg(int physId);

    // -------- 辅助排序 --------
    // 按起始位置排序区间列表
    void sortIntervalsByStart(std::vector<LiveInterval *> &intervals);
    // 按结束位置有序插入到 active_ 列表
    void insertActiveInterval(LiveInterval *interval);
};

#pragma endregion

} // namespace toyc
