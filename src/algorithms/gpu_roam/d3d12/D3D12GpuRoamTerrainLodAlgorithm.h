#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamMeshBuilder.h"

#include <memory>

namespace ParallelRoam::Render
{
class D3D12GraphicsBackend;
}

namespace ParallelRoam::Algorithms::GpuRoam::D3D12
{
struct D3D12GpuRoamState;

class D3D12GpuRoamTerrainLodAlgorithm final : public ITerrainLodAlgorithm
{
public:
    explicit D3D12GpuRoamTerrainLodAlgorithm(Render::D3D12GraphicsBackend& backend);
    ~D3D12GpuRoamTerrainLodAlgorithm() override;

    [[nodiscard]] TerrainLodAlgorithmInfo Info() const override;
    [[nodiscard]] TerrainLodAlgorithmCapabilities Capabilities() const override;
    [[nodiscard]] bool BuildRenderData(
        const TerrainLodBuildInput& input,
        TerrainLodRenderPacket& outPacket,
        std::string* errorMessage) override;
    [[nodiscard]] const TerrainLodStats& Stats() const override;
    void Reset() override;

private:
    Render::D3D12GraphicsBackend* _backend{nullptr};
    DataOrientedRoam::DataOrientedRoamMeshBuilder _cpuTopologyBuilder;
    std::unique_ptr<D3D12GpuRoamState> _state;
    TerrainLodStats _stats{};
};
} // namespace ParallelRoam::Algorithms::GpuRoam::D3D12
