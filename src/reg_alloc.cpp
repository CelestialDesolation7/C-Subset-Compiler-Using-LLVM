#include "reg_alloc.h"
#include <algorithm>
#include <climits>
#include <stack>
#include <unordered_set>

namespace toyc {

#pragma region 物理寄存器比较器

// PhysRegComparator：按优先级升序比较，优先级相同时按 ID 升序
bool PhysRegComparator::operator()(int a, int b) const {
    if ((*physRegs)[a].priority != (*physRegs)[b].priority)
        return (*physRegs)[a].priority < (*physRegs)[b].priority;
    return a < b;
}

#pragma endregion

#pragma region 寄存器信息类实现

/**
 * @brief 构造 RV32I 寄存器信息
 * @details 初始化 x0-x31 共 32 个物理寄存器的属性描述：
 *   - x0(zero), x1(ra), x2(sp), x3(gp), x4(tp), x8(s0/fp) 为保留寄存器
 *   - x5(t0), x6(t1) 保留作为溢出临时寄存器
 *   - x10-x17(a0-a7) 为参数/返回值寄存器，调用者保存，优先分配
 *   - x9(s1), x18-x27(s2-s11) 为被调用者保存寄存器
 *   - x7(t2), x28-x31(t3-t6) 为临时寄存器，调用者保存
 */
RegInfo::RegInfo() : allocatableRegs(PhysRegComparator(&physRegs)) {
    physRegs.resize(32);

    // id, name, callerSaved, calleeSaved, reserved, priority
    physRegs[0] = PhysReg(0, "zero", false, false, true, 999); // x0  硬连线为 0，保留
    physRegs[1] = PhysReg(1, "ra", false, false, true, 999);   // x1  返回地址，保留
    physRegs[2] = PhysReg(2, "sp", false, false, true, 999);   // x2  栈指针，保留
    physRegs[3] = PhysReg(3, "gp", false, false, true, 999);   // x3  全局指针，保留
    physRegs[4] = PhysReg(4, "tp", false, false, true, 999);   // x4  线程指针，保留
    physRegs[5] = PhysReg(5, "t0", true, false, true, 999);    // x5  溢出临时寄存器，保留
    physRegs[6] = PhysReg(6, "t1", true, false, true, 999);    // x6  溢出临时寄存器，保留
    physRegs[7] = PhysReg(7, "t2", true, false, false, 20); // x7  临时寄存器，调用者保存
    physRegs[8] = PhysReg(8, "s0", false, false, true, 999); // x8  帧指针 (s0/fp)，保留
    physRegs[9] = PhysReg(9, "s1", false, true, false, 50);  // x9  被调用者保存
    physRegs[10] = PhysReg(10, "a0", true, false, false, 0); // x10 参数/返回值，优先级最高
    physRegs[11] = PhysReg(11, "a1", true, false, false, 1); // x11 参数寄存器
    physRegs[12] = PhysReg(12, "a2", true, false, false, 2);
    physRegs[13] = PhysReg(13, "a3", true, false, false, 3);
    physRegs[14] = PhysReg(14, "a4", true, false, false, 4);
    physRegs[15] = PhysReg(15, "a5", true, false, false, 5);
    physRegs[16] = PhysReg(16, "a6", true, false, false, 6);
    physRegs[17] = PhysReg(17, "a7", true, false, false, 7);
    physRegs[18] = PhysReg(18, "s2", false, true, false, 40);
    physRegs[19] = PhysReg(19, "s3", false, true, false, 41);
    physRegs[20] = PhysReg(20, "s4", false, true, false, 42);
    physRegs[21] = PhysReg(21, "s5", false, true, false, 43);
    physRegs[22] = PhysReg(22, "s6", false, true, false, 44);
    physRegs[23] = PhysReg(23, "s7", false, true, false, 45);
    physRegs[24] = PhysReg(24, "s8", false, true, false, 46);
    physRegs[25] = PhysReg(25, "s9", false, true, false, 47);
    physRegs[26] = PhysReg(26, "s10", false, true, false, 48);
    physRegs[27] = PhysReg(27, "s11", false, true, false, 49);
    physRegs[28] = PhysReg(28, "t3", true, false, false, 21);
    physRegs[29] = PhysReg(29, "t4", true, false, false, 22);
    physRegs[30] = PhysReg(30, "t5", true, false, false, 23);
    physRegs[31] = PhysReg(31, "t6", true, false, false, 24);

    for (int i = 0; i < 32; ++i)
        if (!physRegs[i].reserved)
            allocatableRegs.insert(i);
}

bool RegInfo::isReserved(int id) const { return physRegs[id].reserved; }
bool RegInfo::isCallerSaved(int id) const { return physRegs[id].callerSaved; }
bool RegInfo::isCalleeSaved(int id) const { return physRegs[id].calleeSaved; }
std::string RegInfo::getRegName(int id) const { return physRegs[id].name; }

#pragma endregion

#pragma region 活跃区间类实现

/**
 * @brief 添加活跃范围并自动合并重叠/相邻区间
 * @param s 范围起始位置
 * @param e 范围结束位置
 * @details 插入新范围后，与已有范围进行重叠和相邻检测，
 *          合并所有可合并的范围，保持 ranges 有序且无重叠
 */
void LiveInterval::addRange(int s, int e) {
    LiveRange nr(s, e);
    std::vector<LiveRange> merged;
    bool placed = false;

    for (auto &r : ranges) {
        if (nr.overlaps(r) || nr.adjacent(r)) {
            nr = LiveRange(std::min(nr.start, r.start), std::max(nr.end, r.end));
        } else if (!placed && nr.start < r.start) {
            merged.push_back(nr);
            merged.push_back(r);
            placed = true;
        } else {
            merged.push_back(r);
        }
    }
    if (!placed)
        merged.push_back(nr);

    // 最终检查合并后仍可能有相邻
    ranges.clear();
    for (auto &r : merged) {
        if (!ranges.empty() && (ranges.back().overlaps(r) || ranges.back().adjacent(r))) {
            ranges.back() = LiveRange(std::min(ranges.back().start, r.start),
                                      std::max(ranges.back().end, r.end));
        } else {
            ranges.push_back(r);
        }
    }
}

/**
 * @brief 检查指定位置是否在活跃区间内
 * @param pos 查询位置
 * @return true 表示该位置被某个 LiveRange 覆盖
 */
bool LiveInterval::contains(int pos) const {
    for (auto &r : ranges)
        if (pos >= r.start && pos <= r.end)
            return true;
    return false;
}

// 返回最早活跃起始位置（空区间返回 INT_MAX）
int LiveInterval::start() const { return ranges.empty() ? INT_MAX : ranges.front().start; }
// 返回最晚活跃结束位置（空区间返回 -1）
int LiveInterval::end() const { return ranges.empty() ? -1 : ranges.back().end; }

/**
 * @brief 检查指定位置是否处于活跃区间的空洞中
 * @param pos 查询位置
 * @return true 表示 pos 在 [start, end] 范围内但不被任何 LiveRange 覆盖
 */
bool LiveInterval::inHole(int pos) const {
    return !empty() && pos >= start() && pos <= end() && !contains(pos);
}

#pragma endregion

#pragma region 活跃性分析实现

/**
 * @brief 执行完整的活跃性分析
 * @param F 目标函数
 * @details 流程：
 *   1. 构建控制流图（前驱/后继关系）
 *   2. 扫描指令，计算每个基本块的 useSet 和 defSet
 *   3. 构建 RPO（逆后序）遍历序列
 *   4. 反向迭代求解 liveIn/liveOut 直到收敛
 */
void LivenessAnalysis::run(ir::Function &F) {
    F.buildCFG();
    computeUseDefSets(F);
    F.rpoOrder = buildRPO(F.entryBlock());
    computeLivenessIteratively(F);
}

/**
 * @brief 计算每个基本块的 useSet 和 defSet
 * @details useSet: 在本块中先使用后定义的寄存器（use-before-def）
 *          defSet: 在本块中被定义的寄存器
 */
void LivenessAnalysis::computeUseDefSets(ir::Function &F) {
    for (auto &block : F.blocks) {
        block->useSet.clear();
        block->defSet.clear();
        block->liveIn.clear();
        block->liveOut.clear();

        std::set<int> localDef;
        for (auto &inst : block->insts) {
            // 先处理 use（use-before-def 语义）
            for (int u : inst->useRegs()) {
                if (localDef.find(u) == localDef.end())
                    block->useSet.insert(u);
            }
            // 再处理 def
            int d = inst->defReg();
            if (d != -1) {
                block->defSet.insert(d);
                localDef.insert(d);
            }
        }
    }
}

/**
 * @brief 构建逆后序（Reverse Post-Order）遍历序列
 * @param entry 函数入口基本块
 * @return RPO 排序的基本块指针列表
 * @details 使用非递归 DFS + 后序收集，最后反转得到 RPO
 */
std::vector<ir::BasicBlock *> LivenessAnalysis::buildRPO(ir::BasicBlock *entry) {
    std::vector<ir::BasicBlock *> order;
    if (!entry)
        return order;

    std::unordered_set<ir::BasicBlock *> visited;
    std::stack<std::pair<ir::BasicBlock *, bool>> stk;
    stk.push({entry, false});

    while (!stk.empty()) {
        auto [bb, processed] = stk.top();
        stk.pop();
        if (processed) {
            order.push_back(bb);
            continue;
        }
        if (!visited.insert(bb).second)
            continue;
        stk.push({bb, true});
        for (auto it = bb->succs.rbegin(); it != bb->succs.rend(); ++it)
            if (visited.find(*it) == visited.end())
                stk.push({*it, false});
    }
    std::reverse(order.begin(), order.end());
    return order;
}

/**
 * @brief 迭代求解 liveIn/liveOut 数据流方程
 * @details 数据流方程：
 *   liveOut(B) = ∪ liveIn(S)，S ∈ succ(B)
 *   liveIn(B)  = useSet(B) ∪ (liveOut(B) - defSet(B))
 * 反复迭代直到所有基本块的 liveIn/liveOut 不再变化
 */
void LivenessAnalysis::computeLivenessIteratively(ir::Function &F) {
    bool changed = true;
    while (changed) {
        changed = false;
        // 反向遍历 RPO 序列
        for (int i = static_cast<int>(F.rpoOrder.size()) - 1; i >= 0; --i) {
            auto *bb = F.rpoOrder[i];

            // liveOut = 所有后继的 liveIn 的并集
            std::set<int> newLiveOut;
            for (auto *succ : bb->succs)
                newLiveOut.insert(succ->liveIn.begin(), succ->liveIn.end());

            // liveIn = useSet ∪ (liveOut - defSet)
            std::set<int> newLiveIn = bb->useSet;
            for (int v : newLiveOut)
                if (bb->defSet.find(v) == bb->defSet.end())
                    newLiveIn.insert(v);

            if (newLiveIn != bb->liveIn || newLiveOut != bb->liveOut) {
                bb->liveIn = newLiveIn;
                bb->liveOut = newLiveOut;
                changed = true;
            }
        }
    }
}

#pragma endregion

#pragma region 活跃区间构建器实现

/**
 * @brief 构造活跃区间构建器
 * @param F         目标函数 IR
 * @param LA        已完成的活跃性分析
 */
LiveIntervalBuilder::LiveIntervalBuilder(ir::Function &F, const LivenessAnalysis &LA)
    : F_(F), LA_(LA) {}

/**
 * @brief 构建所有虚拟寄存器的活跃区间
 * @return vreg → LiveInterval 的映射（仅包含非空区间）
 */
std::unordered_map<int, std::unique_ptr<LiveInterval>> LiveIntervalBuilder::build() {
    std::unordered_map<int, std::unique_ptr<LiveInterval>> intervals;
    for (int vreg = 0; vreg <= F_.maxVregId; ++vreg) {
        auto interval = std::make_unique<LiveInterval>(vreg);
        buildIntervalForVreg(vreg, interval);
        if (!interval->empty())
            intervals[vreg] = std::move(interval);
    }
    return intervals;
}

/**
 * @brief 为单个虚拟寄存器构建精确的活跃区间
 * @param vreg     虚拟寄存器 ID
 * @param interval 输出的活跃区间对象
 * @details 遍历 RPO 顺序的每个基本块，结合 liveIn/liveOut 和块内 def/use 信息，
 *          精确计算活跃范围并添加到 interval 中
 */
void LiveIntervalBuilder::buildIntervalForVreg(int vreg, std::unique_ptr<LiveInterval> &interval) {
    for (auto *bb : F_.rpoOrder) {
        bool liveAtStart = bb->liveIn.count(vreg) > 0;
        bool liveAtEnd = bb->liveOut.count(vreg) > 0;

        // 检查块内是否有 def/use
        bool hasDefUse = false;
        if (!liveAtStart && !liveAtEnd) {
            for (auto &inst : bb->insts) {
                if (inst->defReg() == vreg) {
                    hasDefUse = true;
                    break;
                }
                for (int u : inst->useRegs())
                    if (u == vreg) {
                        hasDefUse = true;
                        break;
                    }
                if (hasDefUse)
                    break;
            }
            if (!hasDefUse)
                continue;
        }

        int rangeStart = liveAtStart ? bb->firstPos() : -1;
        int rangeEnd = liveAtEnd ? bb->lastPos() : -1;

        for (auto &inst : bb->insts) {
            // def
            if (inst->defReg() == vreg) {
                if (rangeStart == -1)
                    rangeStart = inst->posDef();
                rangeEnd = liveAtEnd ? bb->lastPos() : inst->posDef();
            }
            // use
            for (int u : inst->useRegs()) {
                if (u == vreg) {
                    if (rangeStart == -1)
                        rangeStart = liveAtStart ? bb->firstPos() : inst->posUse();
                    rangeEnd = std::max(rangeEnd, inst->posUse());
                    break;
                }
            }
        }

        if (rangeStart != -1 && rangeEnd != -1)
            interval->addRange(rangeStart, rangeEnd);
    }
}

#pragma endregion

#pragma region 线性扫描分配器实现

// 构造函数：初始化寄存器信息、使用标记数组和空闲寄存器池
LinearScanAllocator::LinearScanAllocator(const RegInfo &regInfo)
    : regInfo_(regInfo), freePhysRegs_(PhysRegComparator(&regInfo_.physRegs)) {
    isPhysRegUsed_.resize(32, false);
    initializeFreeRegs();
}

// initializeFreeRegs：将所有可分配寄存器加入空闲池
void LinearScanAllocator::initializeFreeRegs() {
    freePhysRegs_.clear();
    for (int r : regInfo_.allocatableRegs)
        freePhysRegs_.insert(r);
}

/**
 * @brief 对函数执行寄存器分配
 * @param F 目标函数 IR
 * @return 分配结果
 * @details 流程：
 *   1. 处理函数参数（绑定 a0-a7 或栈）
 *   2. 执行活跃性分析
 *   3. 为指令分配线性位置编号
 *   4. 构建活跃区间
 *   5. 执行线性扫描分配
 *   6. 收集使用信息
 */
AllocationResult LinearScanAllocator::allocate(ir::Function &F) {
    result_ = AllocationResult{};
    active_.clear();
    inactive_.clear();
    nextSpillSlot_ = 0;
    allocatedVregs_.clear();
    initializeFreeRegs();

    // 1. 处理函数参数
    processParameters(F.paramVregs);

    // 2. 活跃性分析
    LivenessAnalysis LA;
    LA.run(F);

    // 3. 指令线性化编号
    assignInstrPositions(F);

    // 4. 构建活跃区间
    LiveIntervalBuilder builder(F, LA);
    intervals_ = builder.build();

    if (debugMode_)
        dumpIntervals(intervals_);

    // 5. 执行线性扫描
    result_ = runLinearScan();

    // 6. 收集使用信息
    result_.usedPhysRegs = getUsedPhysRegs();
    result_.calleeSavedRegs = getCalleeSavedRegs();

    return result_;
}

/**
 * @brief 处理函数参数的寄存器绑定
 * @param paramVregs 参数对应的虚拟寄存器列表
 * @details 前 8 个参数绑定到 a0-a7（x10-x17），超出部分分配到栈
 */
void LinearScanAllocator::processParameters(const std::vector<int> &paramVregs) {
    for (size_t i = 0; i < paramVregs.size(); ++i) {
        int vreg = paramVregs[i];
        if (i < 8) {
            int argReg = 10 + static_cast<int>(i); // a0=x10 .. a7=x17
            result_.vregToPhys[vreg] = argReg;
            result_.paramVregToLocation[vreg] = argReg;
            isPhysRegUsed_[argReg] = true;
            freePhysRegs_.erase(argReg);
            allocatedVregs_.insert(vreg);
        } else {
            int stackOffset = static_cast<int>(i - 8 + 1) * 4;
            result_.vregToStack[vreg] = stackOffset;
            result_.paramVregToLocation[vreg] = stackOffset;
            allocatedVregs_.insert(vreg);
        }
    }
}

// assignInstrPositions：按 RPO 顺序为每条指令分配连续编号
void LinearScanAllocator::assignInstrPositions(ir::Function &F) {
    int pos = 0;
    for (auto *block : F.rpoOrder) {
        for (auto &inst : block->insts) {
            inst->index = pos++;
            inst->blockId = block->id;
        }
    }
}

/**
 * @brief 线性扫描分配核心算法（扩展版：支持 active/inactive 转换）
 * @return 分配结果
 * @details 按起始位置排序所有区间，依次处理：
 *   1. 过期回收已结束的活跃区间
 *   2. 处理 active↔inactive 状态转换（区间空洞感知）
 *   3. 已预分配（参数）的区间直接插入 active
 *   4. 有空闲寄存器则分配，否则溢出
 */
AllocationResult LinearScanAllocator::runLinearScan() {

    std::vector<LiveInterval *> sorted;
    for (auto &[vreg, iv] : intervals_)
        sorted.push_back(iv.get());
    sortIntervalsByStart(sorted);

    for (auto *interval : sorted) {
        expireOldIntervals(interval->start());
        handleActiveInactiveTransitions(interval->start());

        if (allocatedVregs_.count(interval->vreg)) {
            // 已预分配（参数寄存器），插入 active 列表
            if (result_.vregToPhys.count(interval->vreg))
                insertActiveInterval(interval);
            continue;
        }

        if (freePhysRegs_.empty()) {
            spillAtInterval(*interval);
        } else {
            allocatePhysicalReg(*interval);
            allocatedVregs_.insert(interval->vreg);
        }
    }
    return result_;
}

// expireOldIntervals：释放在 curStart 之前已经结束的活跃区间占用的物理寄存器
void LinearScanAllocator::expireOldIntervals(int curStart) {
    auto it = active_.begin();
    while (it != active_.end()) {
        if ((*it)->end() < curStart) {
            freePhysReg((*it)->physReg);
            it = active_.erase(it);
        } else {
            break;
        }
    }
}

/**
 * @brief 处理 active↔inactive 状态转换（区间空洞感知）
 * @param curStart 当前处理位置
 * @details
 *   1. 将 active 中在 curStart 处进入空洞的区间移至 inactive（不释放寄存器）
 *   2. 将 inactive 中已完全结束的区间释放寄存器
 *   3. 将 inactive 中在 curStart 处恢复活跃的区间移回 active
 */
void LinearScanAllocator::handleActiveInactiveTransitions(int curStart) {
    // Active → Inactive: 区间在 curStart 处有空洞
    auto it = active_.begin();
    while (it != active_.end()) {
        if ((*it)->inHole(curStart)) {
            inactive_.push_back(*it);
            it = active_.erase(it);
        } else {
            ++it;
        }
    }

    // Inactive → Expired/Active
    auto jt = inactive_.begin();
    while (jt != inactive_.end()) {
        if ((*jt)->end() < curStart) {
            // 完全结束 → 释放寄存器
            freePhysReg((*jt)->physReg);
            jt = inactive_.erase(jt);
        } else if ((*jt)->contains(curStart)) {
            // 恢复活跃 → 移回 active
            insertActiveInterval(*jt);
            jt = inactive_.erase(jt);
        } else {
            ++jt;
        }
    }
}

// allocatePhysicalReg：从空闲池中取出优先级最高的寄存器并插入 active 列表
void LinearScanAllocator::allocatePhysicalReg(LiveInterval &interval) {
    int physReg = allocatePhysReg();
    interval.physReg = physReg;
    result_.vregToPhys[interval.vreg] = physReg;
    insertActiveInterval(&interval);
}

/**
 * @brief 溢出处理：将当前区间或 active/inactive 中结束最晚的区间溢出到栈
 * @param interval 当前要分配的区间
 * @details 同时考虑 active 和 inactive 列表中的区间作为踢出候选。
 *          如果找到结束位置比当前区间更晚的，则溢出该区间，
 *          将其物理寄存器转给当前区间；否则直接溢出当前区间
 */
void LinearScanAllocator::spillAtInterval(LiveInterval &interval) {
    int bestEnd = interval.end();
    LiveInterval *bestSpill = nullptr;
    bool bestFromActive = true;
    int bestIdx = -1;

    // 在 active 列表中找结束最晚的
    for (int i = 0; i < static_cast<int>(active_.size()); ++i) {
        if (active_[i]->end() > bestEnd) {
            bestEnd = active_[i]->end();
            bestSpill = active_[i];
            bestFromActive = true;
            bestIdx = i;
        }
    }

    // 在 inactive 列表中找结束最晚的
    for (int i = 0; i < static_cast<int>(inactive_.size()); ++i) {
        if (inactive_[i]->end() > bestEnd) {
            bestEnd = inactive_[i]->end();
            bestSpill = inactive_[i];
            bestFromActive = false;
            bestIdx = i;
        }
    }

    if (bestSpill != nullptr) {
        int physReg = bestSpill->physReg;

        bestSpill->physReg = -1;
        bestSpill->spillSlot = allocateSpillSlot();
        result_.vregToPhys.erase(bestSpill->vreg);
        result_.vregToStack[bestSpill->vreg] = bestSpill->spillSlot;

        if (bestFromActive) {
            active_.erase(active_.begin() + bestIdx);
        } else {
            inactive_.erase(inactive_.begin() + bestIdx);
        }

        interval.physReg = physReg;
        result_.vregToPhys[interval.vreg] = physReg;
        insertActiveInterval(&interval);
        return;
    }
    // 直接溢出当前 interval
    interval.spillSlot = allocateSpillSlot();
    result_.vregToStack[interval.vreg] = interval.spillSlot;
}

// allocateSpillSlot：分配一个新的溢出栈槽（每次 -4 字节）
int LinearScanAllocator::allocateSpillSlot() { return -(++nextSpillSlot_) * 4; }

// allocatePhysReg：从空闲池中取出优先级最高的寄存器
int LinearScanAllocator::allocatePhysReg() {
    if (freePhysRegs_.empty())
        return -1;
    int reg = *freePhysRegs_.begin();
    freePhysRegs_.erase(freePhysRegs_.begin());
    isPhysRegUsed_[reg] = true;
    return reg;
}

// freePhysReg：将物理寄存器归还空闲池
void LinearScanAllocator::freePhysReg(int physId) {
    if (physId >= 0 && !regInfo_.isReserved(physId))
        freePhysRegs_.insert(physId);
}

// sortIntervalsByStart：按起始位置升序排序区间列表
void LinearScanAllocator::sortIntervalsByStart(std::vector<LiveInterval *> &intervals) {
    std::sort(intervals.begin(), intervals.end(),
              [](LiveInterval *a, LiveInterval *b) { return a->start() < b->start(); });
}

// insertActiveInterval：按结束位置有序插入 active_ 列表
void LinearScanAllocator::insertActiveInterval(LiveInterval *interval) {
    auto it =
        std::lower_bound(active_.begin(), active_.end(), interval,
                         [](LiveInterval *a, LiveInterval *b) { return a->end() < b->end(); });
    active_.insert(it, interval);
}

// allocateSpillTempReg：交替返回 t0/t1 作为溢出临时寄存器
int LinearScanAllocator::allocateSpillTempReg() {
    spillTempCounter_ = !spillTempCounter_;
    return spillTempCounter_ ? spillTempReg1_ : spillTempReg2_;
}

// isSpillTempReg：判断是否为溢出临时寄存器
bool LinearScanAllocator::isSpillTempReg(int regId) const {
    return regId == spillTempReg1_ || regId == spillTempReg2_;
}

// getUsedPhysRegs：收集实际使用过的物理寄存器集合
std::set<int> LinearScanAllocator::getUsedPhysRegs() const {
    std::set<int> used;
    for (int i = 0; i < 32; ++i)
        if (isPhysRegUsed_[i])
            used.insert(i);
    return used;
}

// getCalleeSavedRegs：收集使用过的被调用者保存寄存器（需在函数入口/出口保护）
std::set<int> LinearScanAllocator::getCalleeSavedRegs() const {
    std::set<int> callee;
    for (int i = 0; i < 32; ++i)
        if (isPhysRegUsed_[i] && regInfo_.isCalleeSaved(i))
            callee.insert(i);
    return callee;
}

// dumpIntervals：调试输出所有活跃区间信息
void LinearScanAllocator::dumpIntervals(
    const std::unordered_map<int, std::unique_ptr<LiveInterval>> &intervals) {
    *debugOutput_ << "=== Live Intervals ===\n";
    std::vector<int> vregs;
    for (auto &[v, _] : intervals)
        vregs.push_back(v);
    std::sort(vregs.begin(), vregs.end());
    for (int v : vregs) {
        auto &iv = intervals.at(v);
        *debugOutput_ << "  %vreg" << v << ": ";
        for (auto &r : iv->ranges)
            *debugOutput_ << "[" << r.start << ", " << r.end << ") ";
        *debugOutput_ << "\n";
    }
}

#pragma endregion

} // namespace toyc
