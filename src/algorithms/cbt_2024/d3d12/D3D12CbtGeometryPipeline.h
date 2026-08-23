#pragma once

#include "algorithms/cbt_2024/CbtBisectorTopology.h"

#if !defined(NOMINMAX)
#define NOMINMAX
#endif

#include <d3d12.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace ParallelRoam::Render
{
class D3D12GraphicsBackend;
}

namespace ParallelRoam::Algorithms::Cbt2024::D3D12
{
/// <summary>
/// 持有 CBT 参数几何、最终顶点和对应 compute PSO，并独立维护两项资源状态。
/// 帧编排器只决定何时执行 active/modified 路径，不拥有几何资源。
/// </summary>
class D3D12CbtGeometryPipeline
{
public:
    [[nodiscard]] bool Initialize(
        Render::D3D12GraphicsBackend& backend,
        ID3D12RootSignature* rootSignature,
        const CbtBaseTopology& topology,
        std::string* errorMessage);
    void Shutdown();

    void TransitionClassification(
        ID3D12GraphicsCommandList* commandList,
        D3D12_RESOURCE_STATES nextState);
    void TransitionVertices(
        ID3D12GraphicsCommandList* commandList,
        D3D12_RESOURCE_STATES nextState);

    [[nodiscard]] ID3D12PipelineState* ActivePipeline() const;
    [[nodiscard]] ID3D12PipelineState* ModifiedPipeline() const;
    [[nodiscard]] ID3D12Resource* ClassificationPositions() const;
    [[nodiscard]] ID3D12Resource* RenderVertices() const;
    [[nodiscard]] std::size_t ClassificationPositionCapacityBytes() const;
    [[nodiscard]] std::size_t RenderVertexCapacityBytes() const;

private:
    // active PSO 在全量失效时重建所有活动槽几何。
    Microsoft::WRL::ComPtr<ID3D12PipelineState> _activePipeline;
    // modified PSO 在常规帧只更新本次提交触及的槽位。
    Microsoft::WRL::ComPtr<ID3D12PipelineState> _modifiedPipeline;
    // 分类位置为每个槽保存四组候选三角形顶点。
    Microsoft::WRL::ComPtr<ID3D12Resource> _classificationPositions;
    // 渲染顶点遵循 TerrainMeshVertex 的 52-byte 输入布局。
    Microsoft::WRL::ComPtr<ID3D12Resource> _renderVertices;
    // 容量字节数由算法层写入借用资源契约并由 renderer 校验。
    std::size_t _classificationPositionCapacityBytes{0U};
    std::size_t _renderVertexCapacityBytes{0U};
    D3D12_RESOURCE_STATES _classificationState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    D3D12_RESOURCE_STATES _renderVertexState{D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
};
} // namespace ParallelRoam::Algorithms::Cbt2024::D3D12
