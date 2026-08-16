#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamPipeline.h"

#include <memory>

namespace ParallelRoam::Render
{
class D3D12GraphicsBackend;
}

namespace ParallelRoam::Algorithms::GpuRoam::D3D12
{
struct D3D12GpuRoamState;

/// <summary>
/// 将 DOD CPU 拓扑快照桥接到 D3D12 计算管线的实验算法
/// </summary>
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
    // 后端由 Application 持有，算法只在生命周期内借用
    Render::D3D12GraphicsBackend* _backend{nullptr};
    // 当前实现先通过 DOD CPU ROAM 生成拓扑快照
    DataOrientedRoam::DataOrientedRoamPipeline _cpuTopologyPipeline;
    std::unique_ptr<D3D12GpuRoamState> _state;
    TerrainLodStats _stats{};
};
} // namespace ParallelRoam::Algorithms::GpuRoam::D3D12
