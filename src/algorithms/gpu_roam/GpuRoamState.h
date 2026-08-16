#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace ParallelRoam::Algorithms::GpuRoam
{
inline constexpr std::size_t GpuRoamTimingReadbackSlotCount = 4U;

enum class GpuRoamGpuPass : std::size_t
{
    // 在 GPU 修改前扫描已上传的持久 topology
    InitialLeafCompaction,
    // 将 variance、projection 和 depth 转换为每个 leaf 的 pixel error
    ErrorEvaluation,
    // 根据 split threshold 对 active leaf 分类
    SplitCandidateMarking,
    // 根据 merge threshold 对 split parent 评分，当前混合路径
    // 只记录这些候选，merge topology 仍由 CPU 提交
    MergeCandidateMarking,
    // 提交一轮受 budget 限制的 boundary split 或 direct base-neighbor pair
    // 不发布递归的兼容约束链
    SplitTopology,
    // topology 改变后只清空 dense leaf allocator
    ActiveLeafReset,
    // 从 split 后的 node pool 重建 dense active-leaf list
    FinalLeafCompaction,
    // 将最终 leaf 展开为 vertices/indices，并写入 indirect draw arguments
    MeshEmit,
    Count,
};

inline constexpr std::size_t GpuRoamGpuPassCount =
    static_cast<std::size_t>(GpuRoamGpuPass::Count);

struct GpuRoamGpuPassTimings
{
    // 数值来自延迟 GPU query，而不是 CPU dispatch wall-clock 采样
    // 数组顺序由 GpuRoamGpuPass 固定，并与报告列顺序共享
    // 因此每个结果描述之前已经完成的 ring-buffer slot
    std::array<float, GpuRoamGpuPassCount> Milliseconds{};

    // 各 pass 不重叠，每个 query 都在下一个 query 开始前结束
    // 总和不包含 snapshot 构建、上传、query wait 和渲染
    [[nodiscard]] float SumMilliseconds() const;
};

/// <summary>
/// GPU ROAM-like OpenGL passes 共享的计数器布局
/// </summary>
struct GpuRoamCounters
{
    std::uint32_t ActiveLeafCount{0};
    std::uint32_t SplitCandidateCount{0};
    std::uint32_t MergeCandidateCount{0};
    std::uint32_t RemainingSplitBudget{0};
    std::uint32_t SplitOnlyCommitCount{0};
    std::uint32_t AllocatedNodeCount{0};
    std::uint32_t BudgetRejectedSplitCount{0};
    std::uint32_t Reserved{0};
};

/// <summary>
/// DrawElementsIndirect command 的 GPU buffer 侧布局
/// </summary>
struct GpuRoamDrawElementsIndirectCommand
{
    std::uint32_t Count{0};
    std::uint32_t InstanceCount{1};
    std::uint32_t FirstIndex{0};
    std::int32_t BaseVertex{0};
    std::uint32_t BaseInstance{0};
};

/// <summary>
/// 延迟回读环中的单个 query 和 counter 配对槽位
/// </summary>
struct GpuRoamTimingReadbackSlot
{
    // query、counter 和 capacity snapshot 必须来自同一次提交
    // 每个 pass 保留一个 query object，因为 GL_TIME_ELAPSED query 不能嵌套
    // 只有 ResolveTimingReadbackSlot 消费该 slot 后才能复用整个 slot
    std::array<std::uint32_t, GpuRoamGpuPassCount> TimerQueryIds{};
    // CounterBufferId 保存这条确切 pass 链生成的 topology counter
    std::uint32_t CounterBufferId{0};
    // capacity 单调增长，并在后续 slot 轮换中复用
    std::size_t CounterBufferCapacityBytes{0};
    // base count 用于在延迟回读中校验 split 守恒，不依赖当前状态
    std::size_t BaseActiveLeafCount{0};
    std::size_t BaseNodeCount{0};
    // output capacity 随提交冻结，用于边界校验
    std::size_t ActiveLeafCapacity{0};
    std::size_t NodeCapacity{0};
    // budget 守恒通过 remaining token 加 committed split 校验
    std::size_t InitialSplitBudget{0};
    // Pending 用于区分尚未提交或已消费的 slot 与可读取提交
    bool Pending{false};
};

/// <summary>
/// GPU ROAM-like builder 的可复用 OpenGL resource state
/// </summary>
class GpuRoamState
{
public:
    ~GpuRoamState();

    GpuRoamState() = default;
    GpuRoamState(const GpuRoamState&) = delete;
    GpuRoamState& operator=(const GpuRoamState&) = delete;
    GpuRoamState(GpuRoamState&&) = delete;
    GpuRoamState& operator=(GpuRoamState&&) = delete;

    void Reset();

    // 资源 ID 按节点池、工作集、候选和绘制输出分组持有
    std::uint32_t NodeBufferId{0};
    std::uint32_t ActiveLeafBufferId{0};
    std::uint32_t HeightMapTextureId{0};
    std::uint32_t ScreenErrorBufferId{0};
    std::uint32_t CounterBufferId{0};
    std::uint32_t SplitCandidateBufferId{0};
    std::uint32_t MergeCandidateBufferId{0};
    std::uint32_t GpuVertexBufferId{0};
    std::uint32_t GpuIndexBufferId{0};
    std::uint32_t IndirectDrawBufferId{0};

    std::uint32_t ActiveLeafCompactionProgramId{0};
    std::uint32_t ActiveLeafResetProgramId{0};
    std::uint32_t ErrorEvaluationProgramId{0};
    std::uint32_t CandidateMarkingProgramId{0};
    std::uint32_t MeshEmitProgramId{0};
    std::uint32_t SplitOnlyTopologyProgramId{0};

    // 容量只控制是否扩容，不能作为当前有效元素数量
    std::size_t NodeBufferCapacityBytes{0};
    std::size_t ActiveLeafBufferCapacityBytes{0};
    std::size_t ScreenErrorBufferCapacityBytes{0};
    std::size_t SplitCandidateBufferCapacityBytes{0};
    std::size_t MergeCandidateBufferCapacityBytes{0};
    std::size_t GpuVertexBufferCapacityBytes{0};
    std::size_t GpuIndexBufferCapacityBytes{0};
    std::size_t IndirectDrawBufferCapacityBytes{0};

    // 高度图纹理只在资源路径或尺寸变化时重新上传
    std::filesystem::path CachedHeightMapPath;
    int CachedHeightMapWidth{0};
    int CachedHeightMapHeight{0};
    bool HeightMapTextureUploaded{false};

    // 延迟 ring 只暴露最近完成且一致的结果
    // TimingReadbackCursor 选择待解析的 slot，并立即复用
    // LastCompletedCounters 和 timings 始终来自同一个已解析 slot
    // 第一帧还没有已解析 slot，因此所有延迟值保持为 0
    // 这种设计避免等待刚刚 dispatch 的帧
    GpuRoamTimingReadbackSlot TimingReadbackSlots[GpuRoamTimingReadbackSlotCount]{};
    std::size_t TimingReadbackCursor{0};
    GpuRoamCounters LastCompletedCounters{};
    GpuRoamGpuPassTimings LastCompletedGpuPassTimings{};
    bool HasCompletedTimingReadback{false};
};
} // namespace ParallelRoam::Algorithms::GpuRoam
