#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace ParallelRoam::Algorithms::GpuRoam
{
inline constexpr std::size_t GpuRoamTimingReadbackSlotCount = 4U;

/// <summary>
/// GPU ROAM-like OpenGL passes 共享的计数器布局
/// </summary>
struct GpuRoamCounters
{
    std::uint32_t ActiveLeafCount{0};
    std::uint32_t SplitCandidateCount{0};
    std::uint32_t MergeCandidateCount{0};
    std::uint32_t Reserved{0};
    std::uint32_t SplitOnlyCommitCount{0};
    std::uint32_t AllocatedNodeCount{0};
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
    // query、counter 和容量快照必须来自同一次提交
    std::uint32_t TimerQueryId{0};
    std::uint32_t CounterBufferId{0};
    std::size_t CounterBufferCapacityBytes{0};
    std::size_t BaseActiveLeafCount{0};
    std::size_t BaseNodeCount{0};
    std::size_t ActiveLeafCapacity{0};
    std::size_t NodeCapacity{0};
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

    // 延迟环只公开最近完成的结果，避免统计路径强制等待 GPU
    GpuRoamTimingReadbackSlot TimingReadbackSlots[GpuRoamTimingReadbackSlotCount]{};
    std::size_t TimingReadbackCursor{0};
    GpuRoamCounters LastCompletedCounters{};
    float LastCompletedGpuComputeMilliseconds{0.0F};
    bool HasCompletedTimingReadback{false};
};
} // namespace ParallelRoam::Algorithms::GpuRoam
