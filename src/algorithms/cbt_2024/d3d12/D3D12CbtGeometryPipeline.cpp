#include "algorithms/cbt_2024/d3d12/D3D12CbtGeometryPipeline.h"

#include "render/D3D12GraphicsBackend.h"
#include "terrain/TerrainMeshBuilder.h"

#include <filesystem>
#include <fstream>
#include <vector>

namespace ParallelRoam::Algorithms::Cbt2024::D3D12
{
namespace
{
using Microsoft::WRL::ComPtr;

void SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
}

std::vector<std::uint8_t> ReadBinaryFile(
    const std::filesystem::path& path,
    std::string* errorMessage)
{
    // shader 由 CMake/DXC 在构建期生成，运行期只装载固定 CSO。
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream)
    {
        SetError(errorMessage, "Failed to open compiled CBT geometry shader: " + path.string());
        return {};
    }
    const std::streamsize size = stream.tellg();
    if (size <= 0)
    {
        SetError(errorMessage, "Compiled CBT geometry shader is empty: " + path.string());
        return {};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        SetError(errorMessage, "Failed to read compiled CBT geometry shader: " + path.string());
        return {};
    }
    return bytes;
}

bool CreateBuffer(
    ID3D12Device* device,
    std::size_t bytes,
    ComPtr<ID3D12Resource>& resource,
    std::string* errorMessage)
{
    // 分类位置和最终顶点都由 compute 写入，因此常驻 default heap UAV。
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = bytes;
    description.Height = 1U;
    description.DepthOrArraySize = 1U;
    description.MipLevels = 1U;
    description.SampleDesc.Count = 1U;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &description,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&resource))))
    {
        SetError(errorMessage, "Failed to allocate CBT geometry buffer");
        return false;
    }
    return true;
}

bool CreatePipeline(
    ID3D12Device* device,
    ID3D12RootSignature* rootSignature,
    const std::filesystem::path& shaderPath,
    ComPtr<ID3D12PipelineState>& pipeline,
    std::string* errorMessage)
{
    // 两条几何路径共享 bootstrap root signature，但拥有独立入口 PSO。
    const std::vector<std::uint8_t> shader = ReadBinaryFile(shaderPath, errorMessage);
    if (shader.empty())
    {
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
    description.pRootSignature = rootSignature;
    description.CS = {shader.data(), shader.size()};
    if (FAILED(device->CreateComputePipelineState(&description, IID_PPV_ARGS(&pipeline))))
    {
        SetError(errorMessage, "Failed to create CBT geometry pipeline: " + shaderPath.string());
        return false;
    }
    return true;
}

void Transition(
    ID3D12GraphicsCommandList* commandList,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES& currentState,
    D3D12_RESOURCE_STATES nextState)
{
    // 状态镜像与 barrier 同步更新，重复请求不会产生冗余 transition。
    if (resource == nullptr || currentState == nextState)
    {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = currentState;
    barrier.Transition.StateAfter = nextState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1U, &barrier);
    currentState = nextState;
}
} // namespace

bool D3D12CbtGeometryPipeline::Initialize(
    Render::D3D12GraphicsBackend& backend,
    ID3D12RootSignature* rootSignature,
    const CbtBaseTopology& topology,
    std::string* errorMessage)
{
    // 重建先释放旧几何代，避免容量变化后保留不匹配的顶点缓冲。
    Shutdown();
#if defined(PARALLEL_ROAM_DX12_SHADER_DIR)
    const std::filesystem::path shaderDirectory{PARALLEL_ROAM_DX12_SHADER_DIR};
#else
    const std::filesystem::path shaderDirectory{"assets/shaders/dx12"};
#endif
    // 每个槽保留四组三点分类位置；绘制缓冲则是每槽一个最终三角形。
    const std::size_t totalCount = topology.Layout.TotalElementCount;
    _classificationPositionCapacityBytes = totalCount * 4U * sizeof(float) * 3U;
    _renderVertexCapacityBytes = totalCount * 3U * sizeof(Terrain::TerrainMeshVertex);
    if (!CreatePipeline(
            backend.Device(),
            rootSignature,
            shaderDirectory / "CbtBootstrapBuildActiveGeometry.cso",
            _activePipeline,
            errorMessage) ||
        !CreatePipeline(
            backend.Device(),
            rootSignature,
            shaderDirectory / "CbtBootstrapBuildModifiedGeometry.cso",
            _modifiedPipeline,
            errorMessage) ||
        !CreateBuffer(
            backend.Device(),
            _classificationPositionCapacityBytes,
            _classificationPositions,
            errorMessage) ||
        !CreateBuffer(
            backend.Device(),
            _renderVertexCapacityBytes,
            _renderVertices,
            errorMessage))
    {
        Shutdown();
        return false;
    }
    return true;
}

void D3D12CbtGeometryPipeline::Shutdown()
{
    // 资源释放后把状态镜像恢复为 CreateBuffer 契约的 UAV 初值。
    _activePipeline.Reset();
    _modifiedPipeline.Reset();
    _classificationPositions.Reset();
    _renderVertices.Reset();
    _classificationPositionCapacityBytes = 0U;
    _renderVertexCapacityBytes = 0U;
    _classificationState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _renderVertexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
}

void D3D12CbtGeometryPipeline::TransitionClassification(
    ID3D12GraphicsCommandList* commandList,
    D3D12_RESOURCE_STATES nextState)
{
    // 分类位置在 classify 时作为 SRV，在几何更新时作为 UAV。
    Transition(commandList, _classificationPositions.Get(), _classificationState, nextState);
}

void D3D12CbtGeometryPipeline::TransitionVertices(
    ID3D12GraphicsCommandList* commandList,
    D3D12_RESOURCE_STATES nextState)
{
    // 最终顶点在 compute 写入与 renderer 顶点读取之间切换。
    Transition(commandList, _renderVertices.Get(), _renderVertexState, nextState);
}

ID3D12PipelineState* D3D12CbtGeometryPipeline::ActivePipeline() const
{
    return _activePipeline.Get();
}

ID3D12PipelineState* D3D12CbtGeometryPipeline::ModifiedPipeline() const
{
    return _modifiedPipeline.Get();
}

ID3D12Resource* D3D12CbtGeometryPipeline::ClassificationPositions() const
{
    return _classificationPositions.Get();
}

ID3D12Resource* D3D12CbtGeometryPipeline::RenderVertices() const
{
    return _renderVertices.Get();
}

std::size_t D3D12CbtGeometryPipeline::ClassificationPositionCapacityBytes() const
{
    return _classificationPositionCapacityBytes;
}

std::size_t D3D12CbtGeometryPipeline::RenderVertexCapacityBytes() const
{
    return _renderVertexCapacityBytes;
}
} // namespace ParallelRoam::Algorithms::Cbt2024::D3D12
