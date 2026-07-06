#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"

#include <string>

namespace ParallelRoam::Algorithms::GpuRoam
{
/// <summary>
/// GPU ROAM-like 的阶段 4A 适配器，先稳定统一接口和能力门禁
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
    TerrainLodStats _stats{};
};

[[nodiscard]] std::string GpuRoamLikeUnavailableReason();
} // namespace ParallelRoam::Algorithms::GpuRoam
