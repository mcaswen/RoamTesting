#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamMeshBuilder.h"

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
/// <summary>
/// 将 3B SoA Data-Oriented ROAM 接入三版本共享的 Terrain LOD 算法接口
/// </summary>
class DataOrientedRoamTerrainLodAlgorithm final : public ITerrainLodAlgorithm
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
    [[nodiscard]] static DataOrientedRoamSettings ToDataOrientedSettings(const TerrainLodSettings& settings);
    [[nodiscard]] static TerrainLodStats ToTerrainLodStats(const DataOrientedRoamStats& stats);

    DataOrientedRoamMeshBuilder _builder;
    TerrainLodStats _stats;
};
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
