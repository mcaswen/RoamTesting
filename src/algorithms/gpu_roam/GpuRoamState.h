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
    // Scans the uploaded persistent topology before any GPU-side mutation.
    InitialLeafCompaction,
    // Converts variance, projection and depth into per-leaf pixel error.
    ErrorEvaluation,
    // Classifies active leaves against the split threshold.
    SplitCandidateMarking,
    // Scores split parents against the merge threshold. The current hybrid
    // path records these candidates but commits merge topology on CPU.
    MergeCandidateMarking,
    // Commits one budget-limited wave of boundary splits or direct
    // base-neighbor pairs. It does not publish a recursive compatibility chain.
    SplitTopology,
    // Clears only the dense-leaf allocator after topology has changed.
    ActiveLeafReset,
    // Rebuilds the dense active-leaf list from the post-split node pool.
    FinalLeafCompaction,
    // Expands final leaves into vertices/indices and writes indirect draw arguments.
    MeshEmit,
    Count,
};

inline constexpr std::size_t GpuRoamGpuPassCount =
    static_cast<std::size_t>(GpuRoamGpuPass::Count);

struct GpuRoamGpuPassTimings
{
    // Values come from delayed GPU queries, not CPU dispatch wall-clock samples.
    // The array order is fixed by GpuRoamGpuPass and shared with report columns.
    // A result therefore describes the previously completed ring-buffer slot.
    std::array<float, GpuRoamGpuPassCount> Milliseconds{};

    // Passes do not overlap: every query ends before the next query begins.
    // The sum excludes snapshot building, uploads, query waits and rendering.
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
    // Query, counter and capacity snapshots must come from the same submission.
    // One query object is retained per pass because GL_TIME_ELAPSED queries cannot nest.
    // Reusing the whole slot is legal only after ResolveTimingReadbackSlot consumes it.
    std::array<std::uint32_t, GpuRoamGpuPassCount> TimerQueryIds{};
    // CounterBufferId stores the topology counters produced by this exact pass chain.
    std::uint32_t CounterBufferId{0};
    // Capacity is monotonically grown and reused across subsequent slot rotations.
    std::size_t CounterBufferCapacityBytes{0};
    // Base counts let delayed readback verify split conservation without current state.
    std::size_t BaseActiveLeafCount{0};
    std::size_t BaseNodeCount{0};
    // Output capacities are frozen with the submission for bounds validation.
    std::size_t ActiveLeafCapacity{0};
    std::size_t NodeCapacity{0};
    // Budget conservation is checked as remaining tokens plus committed splits.
    std::size_t InitialSplitBudget{0};
    // Pending separates never-submitted/consumed slots from readable submissions.
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

    // The delayed ring exposes only the most recently completed coherent result.
    // TimingReadbackCursor selects the slot to resolve and immediately reuse.
    // LastCompletedCounters and timings always refer to the same resolved slot.
    // A first frame has no resolved slot, so all delayed values remain zero.
    // This design avoids waiting on the frame that has just been dispatched.
    GpuRoamTimingReadbackSlot TimingReadbackSlots[GpuRoamTimingReadbackSlotCount]{};
    std::size_t TimingReadbackCursor{0};
    GpuRoamCounters LastCompletedCounters{};
    GpuRoamGpuPassTimings LastCompletedGpuPassTimings{};
    bool HasCompletedTimingReadback{false};
};
} // namespace ParallelRoam::Algorithms::GpuRoam
