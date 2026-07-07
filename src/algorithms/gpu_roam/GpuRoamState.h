#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace ParallelRoam::Algorithms::GpuRoam
{
inline constexpr std::size_t GpuRoamTimingReadbackSlotCount = 4U;

/// <summary>
/// GPU ROAM-like 路径持有的 OpenGL 资源和跨 pass 计数器布局
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

struct GpuRoamTimingReadbackSlot
{
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
    std::size_t NodeBufferCapacityBytes{0};
    std::size_t ActiveLeafBufferCapacityBytes{0};
    std::size_t ScreenErrorBufferCapacityBytes{0};
    std::size_t SplitCandidateBufferCapacityBytes{0};
    std::size_t MergeCandidateBufferCapacityBytes{0};
    std::size_t GpuVertexBufferCapacityBytes{0};
    std::size_t GpuIndexBufferCapacityBytes{0};
    std::size_t IndirectDrawBufferCapacityBytes{0};
    std::filesystem::path CachedHeightMapPath;
    int CachedHeightMapWidth{0};
    int CachedHeightMapHeight{0};
    bool HeightMapTextureUploaded{false};
    GpuRoamTimingReadbackSlot TimingReadbackSlots[GpuRoamTimingReadbackSlotCount]{};
    std::size_t TimingReadbackCursor{0};
    GpuRoamCounters LastCompletedCounters{};
    float LastCompletedGpuComputeMilliseconds{0.0F};
    bool HasCompletedTimingReadback{false};
};
} // namespace ParallelRoam::Algorithms::GpuRoam
