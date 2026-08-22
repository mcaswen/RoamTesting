#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/cbt_2024/CbtBisectorTopology.h"
#include "algorithms/cbt_2024/d3d12/D3D12CbtBaseTopology.h"
#include "render/D3D12GraphicsBackend.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ParallelRoam::Algorithms::Cbt2024::D3D12
{
/// <summary>
/// E0 的正式 GPU 事务：Reset、生产 OCBT Reduce、Indexation 和平面几何 Bootstrap
///
/// 生命周期约束：
/// - Topology 由 D3D12CbtTerrainState 持有，并且必须比本对象更晚销毁；
/// - Initialize 只创建管线、描述符和几何输出，不提交独立 command queue；
/// - RecordFrame 只能在 backend BeginFrame/Present 区间调用；
/// - RenderVertices 返回借用资源，renderer 不取得 COM 所有权；
/// - Shutdown 归还连续描述符并清除全部状态镜像。
///
/// 帧内顺序：
/// - Reset 清空 transient task、validation、draw 和 dispatch 状态；
/// - 三段生产 Reduce 从 occupancy bitfield 重建 OCBT 计数；
/// - Indexation 在 GPU 上派生活动、可见和修改列表；
/// - PrepareIndirect 生成 draw/dispatch 参数；
/// - 可选 base geometry pass 生成与仓库渲染器兼容的 52-byte 顶点；
/// - 最终 transition 发布 SRV 与 INDIRECT_ARGUMENT 资源。
///
/// CPU 从不读回 live draw count。TopologyFrameGeneration 只是调度诊断，
/// 不参与 ExecuteIndirect 参数计算，也不能替代 GPU draw state。
/// </summary>
class D3D12CbtE0Pipeline
{
public:
    D3D12CbtE0Pipeline() = default;
    ~D3D12CbtE0Pipeline();

    D3D12CbtE0Pipeline(const D3D12CbtE0Pipeline&) = delete;
    D3D12CbtE0Pipeline& operator=(const D3D12CbtE0Pipeline&) = delete;

    [[nodiscard]] bool Initialize(
        Render::D3D12GraphicsBackend& backend,
        const CbtBaseTopology& topology,
        const D3D12CbtTopologyResourceView& resources,
        std::string* errorMessage);
    void Shutdown();

    [[nodiscard]] bool RecordFrame(
        const TerrainLodBuildInput& input,
        const CbtBaseTopology& topology,
        const D3D12CbtTopologyResourceView& resources,
        bool rebuildGeometry,
        std::string* errorMessage);

    [[nodiscard]] ID3D12Resource* RenderVertices() const;
    [[nodiscard]] ID3D12Resource* ClassificationPositions() const;
    [[nodiscard]] std::size_t RenderVertexCapacityBytes() const;
    [[nodiscard]] std::size_t ClassificationPositionCapacityBytes() const;
    [[nodiscard]] std::uint64_t TopologyFrameGeneration() const;
    [[nodiscard]] bool IsInitialized() const;

private:
    // 每档容量各有一组编译期定长的 OCBT shader。
    // Reset 与三段 Reduce 共享同一生产 root signature。
    struct CapacityPipelines
    {
        Microsoft::WRL::ComPtr<ID3D12PipelineState> Reset;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> ReducePre;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> ReduceFirst;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> ReduceSecond;
    };

    [[nodiscard]] bool CreateTopologyRootSignature(std::string* errorMessage);
    [[nodiscard]] bool CreateBootstrapRootSignature(std::string* errorMessage);
    [[nodiscard]] bool CreatePipelines(std::string* errorMessage);
    [[nodiscard]] bool CreateConstantBuffers(std::string* errorMessage);
    [[nodiscard]] bool CreateGeometryBuffers(
        const CbtBaseTopology& topology,
        std::string* errorMessage);
    [[nodiscard]] bool ConfigureTopologyDescriptors(
        const CbtBaseTopology& topology,
        const D3D12CbtTopologyResourceView& resources,
        std::string* errorMessage);

    // 管线对象和连续描述符只在 Initialize/Shutdown 边界变化。
    Render::D3D12GraphicsBackend* _backend{nullptr};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> _topologyRootSignature;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> _bootstrapRootSignature;
    std::array<CapacityPipelines, 4> _capacityPipelines;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> _indexationPipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> _prepareIndirectPipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> _baseGeometryPipeline;
    Render::D3D12DescriptorAllocation _topologyUavRange;

    // 帧常量按 swap-chain frame 隔离并在整个管线生命周期中保持映射。
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, Render::D3D12GraphicsBackend::FrameCount> _constantBuffers;
    std::array<std::uint8_t*, Render::D3D12GraphicsBackend::FrameCount> _mappedConstants{};
    Microsoft::WRL::ComPtr<ID3D12Resource> _classificationPositions;
    Microsoft::WRL::ComPtr<ID3D12Resource> _renderVertices;
    std::size_t _classificationPositionCapacityBytes{0U};
    std::size_t _renderVertexCapacityBytes{0U};

    // 下列状态镜像覆盖所有会在 compute、vertex 和 indirect 角色间切换的资源。
    // BaseTopology 首次上传后的公开状态统一为 UAV，所以初值与它的契约一致。
    // 每个 transition helper 同步更新镜像，避免发出 before-state 错误的 barrier。
    D3D12_RESOURCE_STATES _heapIdState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    D3D12_RESOURCE_STATES _bisectorDataState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    D3D12_RESOURCE_STATES _baseControlPointState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    D3D12_RESOURCE_STATES _activeIndexState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    D3D12_RESOURCE_STATES _visibleIndexState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    D3D12_RESOURCE_STATES _modifiedIndexState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    D3D12_RESOURCE_STATES _drawState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    D3D12_RESOURCE_STATES _geometryDispatchState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    D3D12_RESOURCE_STATES _classificationPositionState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    D3D12_RESOURCE_STATES _renderVertexState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS};

    // generation 只统计成功记录的完整 E0 帧事务。
    // 它用于测试和诊断，不决定 GPU draw 数量或资源生命周期。
    CbtOccupancyCapacity _capacity{CbtOccupancyCapacity::Capacity128K};
    std::uint64_t _topologyFrameGeneration{0U};
    bool _initialized{false};
};
} // namespace ParallelRoam::Algorithms::Cbt2024::D3D12
