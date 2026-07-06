#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamMeshBuilder.h"
#include "algorithms/gpu_roam/GpuRoamMeshBuilder.h"

#include <string>

namespace ParallelRoam::Algorithms::GpuRoam
{
/// <summary>
/// GPU ROAM-like 适配器，负责能力门禁、GPU buffer 输出和统一统计
/// </summary>
class GpuRoamTerrainLodAlgorithm final : public ITerrainLodAlgorithm
{
public:
    [[nodiscard]] TerrainLodAlgorithmInfo Info() const override;
    [[nodiscard]] TerrainLodAlgorithmCapabilities Capabilities() const override;

    [[nodiscard]] bool BuildRenderData(
        const TerrainLodBuildInput& input,
        TerrainLodRenderPacket& outPacket,
        std::string* errorMessage) override;

    [[nodiscard]] const TerrainLodStats& Stats() const override;
    void Reset() override;

private:
    DataOrientedRoam::DataOrientedRoamMeshBuilder _cpuTopologyBuilder;
    GpuRoamMeshBuilder _gpuMeshBuilder;
    TerrainLodStats _stats{};
};

[[nodiscard]] std::string GpuRoamLikeUnavailableReason();
} // namespace ParallelRoam::Algorithms::GpuRoam
