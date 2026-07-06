#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamMeshBuilder.h"

#include <cstdint>
#include <string>

namespace ParallelRoam::Algorithms::GpuRoam
{
/// <summary>
/// GPU ROAM-like 的阶段 4A 适配器，先稳定统一接口和能力门禁
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

    DataOrientedRoam::DataOrientedRoamMeshBuilder _cpuTopologyBuilder;
    TerrainLodStats _stats{};
    std::uint32_t _nodeBufferId{0};
    std::uint32_t _activeLeafBufferId{0};
    std::uint32_t _heightMapTextureId{0};
};

[[nodiscard]] std::string GpuRoamLikeUnavailableReason();
} // namespace ParallelRoam::Algorithms::GpuRoam
