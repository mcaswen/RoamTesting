#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamMeshBuilder.h"

#include <cstdint>
#include <string>

namespace ParallelRoam::Algorithms::GpuRoam
{
struct GpuRoamBufferSnapshot;

/// <summary>
/// GPU ROAM-like 适配器，负责能力门禁、GPU buffer 输出和统一统计
/// </summary>
class GpuRoamTerrainLodAlgorithm final : public ITerrainLodAlgorithm
{
public:
    ~GpuRoamTerrainLodAlgorithm() override;

    [[nodiscard]] TerrainLodAlgorithmInfo Info() const override;
    [[nodiscard]] TerrainLodAlgorithmCapabilities Capabilities() const override;

    [[nodiscard]] bool BuildRenderData(
        const TerrainLodBuildInput& input,
        TerrainLodRenderPacket& outPacket,
        std::string* errorMessage) override;

    [[nodiscard]] const TerrainLodStats& Stats() const override;
    void Reset() override;

private:
    void DestroyGpuResources();
    [[nodiscard]] bool RunGpuComputePipeline(
        const GpuRoamBufferSnapshot& snapshot,
        const TerrainLodBuildInput& input,
        std::size_t& uploadBytes,
        float& gpuComputeMilliseconds,
        std::size_t& readbackBytes,
        std::size_t& gpuActiveLeafCount,
        std::size_t& gpuNodeCount,
        std::size_t& gpuSplitOnlyCommitCount,
        std::string* errorMessage);

    DataOrientedRoam::DataOrientedRoamMeshBuilder _cpuTopologyBuilder;
    TerrainLodStats _stats{};
    std::uint32_t _nodeBufferId{0};
    std::uint32_t _activeLeafBufferId{0};
    std::uint32_t _heightMapTextureId{0};
    std::uint32_t _screenErrorBufferId{0};
    std::uint32_t _counterBufferId{0};
    std::uint32_t _splitCandidateBufferId{0};
    std::uint32_t _mergeCandidateBufferId{0};
    std::uint32_t _gpuVertexBufferId{0};
    std::uint32_t _gpuIndexBufferId{0};
    std::uint32_t _indirectDrawBufferId{0};
    std::uint32_t _activeLeafCompactionProgramId{0};
    std::uint32_t _errorEvaluationProgramId{0};
    std::uint32_t _candidateMarkingProgramId{0};
    std::uint32_t _meshEmitProgramId{0};
    std::uint32_t _splitOnlyTopologyProgramId{0};
    std::uint32_t _timerQueryId{0};
};

[[nodiscard]] std::string GpuRoamLikeUnavailableReason();
} // namespace ParallelRoam::Algorithms::GpuRoam
