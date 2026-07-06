#pragma once

#include <cstdint>

namespace ParallelRoam::Algorithms::GpuRoam
{
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
    std::uint32_t TimerQueryId{0};
};
} // namespace ParallelRoam::Algorithms::GpuRoam
