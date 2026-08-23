#include "algorithms/cbt_2024/d3d12/D3D12CbtE0Pipeline.h"

#include "algorithms/cbt_2024/CbtClassification.h"
#include "algorithms/cbt_2024/CbtSplitPlanner.h"
#include "terrain/TerrainMeshBuilder.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace ParallelRoam::Algorithms::Cbt2024::D3D12
{
namespace
{
using Microsoft::WRL::ComPtr;

// 常量缓冲按 D3D12 CBV 对齐要求分成三个独立的 256 字节槽。
// b0 保存全局视图常量，供后续分类阶段沿用官方协议。
// b1 保存 topology/geometry 的整数边界和深度。
// b2 保存逐帧分类阈值与 drawable 尺寸。
// 每个 swap-chain frame 拥有独立映射区，CPU 不会覆盖尚未消费的数据。
constexpr std::size_t ConstantBufferSlotBytes = 256U;
constexpr std::size_t ConstantBufferBytes = ConstantBufferSlotBytes * 3U;
constexpr std::size_t ClassificationCounterOffset = 0U;
constexpr std::size_t AllocationCounterOffset = sizeof(std::uint32_t) * 2U;
constexpr std::size_t MemoryCounterOffset = sizeof(std::uint32_t) * 3U;
constexpr std::size_t ValidationCounterOffset = sizeof(std::uint32_t) * 5U;
constexpr std::size_t OccupancyRootOffset =
    ValidationCounterOffset + sizeof(std::uint32_t) * 12U;
constexpr std::size_t DrawStateReadbackOffset =
    OccupancyRootOffset + sizeof(std::uint32_t);
constexpr std::size_t BaseBisectorDataOffset =
    DrawStateReadbackOffset + sizeof(CbtDrawState);
constexpr std::size_t E3DiagnosticReadbackBytes = OccupancyRootOffset;
constexpr std::size_t E3ValidationReadbackBytes =
    BaseBisectorDataOffset + sizeof(CbtBisectorData) * CbtBaseBisectorCount;
constexpr std::uint32_t WorkGroupSize = 64U;
constexpr std::uint32_t TopologyUavCount = 17U;

void SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
}

// 所有 E0 bootstrap kernel 固定为 64 线程；这里统一向上取整 dispatch 数量。
std::uint32_t DispatchCount(std::uint32_t count)
{
    return (count + WorkGroupSize - 1U) / WorkGroupSize;
}

// 四档 OCBT 的宏布局不同，必须选择与资源容量完全相同的预编译 PSO。
// 未知枚举只作为防御性回退；公开布局构造器会先拒绝非法容量。
std::size_t CapacityIndex(CbtOccupancyCapacity capacity)
{
    switch (capacity)
    {
    case CbtOccupancyCapacity::Capacity128K: return 0U;
    case CbtOccupancyCapacity::Capacity256K: return 1U;
    case CbtOccupancyCapacity::Capacity512K: return 2U;
    case CbtOccupancyCapacity::Capacity1M: return 3U;
    }
    return 0U;
}

// 运行时只读取 CMake/DXC 已生成的字节码，不在首帧动态编译 shader。
// 初始化失败保留具体路径，便于诊断部署时遗漏的 cso。
std::vector<std::uint8_t> ReadBinaryFile(const std::filesystem::path& path, std::string* errorMessage)
{
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream)
    {
        SetError(errorMessage, "Failed to open CBT topology shader: " + path.string());
        return {};
    }
    const std::streamsize size = stream.tellg();
    if (size <= 0)
    {
        SetError(errorMessage, "CBT topology shader is empty: " + path.string());
        return {};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        SetError(errorMessage, "Failed to read CBT topology shader: " + path.string());
        return {};
    }
    return bytes;
}

// E0 的常量缓冲位于 upload heap，拓扑和几何输出位于 default heap。
D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CreationNodeMask = 1U;
    properties.VisibleNodeMask = 1U;
    return properties;
}

// 所有 CBT 资源都是线性 buffer；最小四字节宽度避免创建空 D3D12 资源。
D3D12_RESOURCE_DESC BufferDescription(std::size_t bytes, D3D12_RESOURCE_FLAGS flags)
{
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = std::max<std::size_t>(bytes, 4U);
    description.Height = 1U;
    description.DepthOrArraySize = 1U;
    description.MipLevels = 1U;
    description.SampleDesc.Count = 1U;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    description.Flags = flags;
    return description;
}

// 集中创建 committed buffer，确保每条资源路径使用相同的 node mask 和布局。
// 调用者显式提供 initialState，状态跟踪器从该值开始维护。
bool CreateBuffer(
    ID3D12Device* device,
    D3D12_HEAP_TYPE heapType,
    std::size_t bytes,
    D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES initialState,
    ComPtr<ID3D12Resource>& resource,
    std::string* errorMessage)
{
    const D3D12_HEAP_PROPERTIES heap = HeapProperties(heapType);
    const D3D12_RESOURCE_DESC description = BufferDescription(bytes, flags);
    if (FAILED(device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &description,
            initialState,
            nullptr,
            IID_PPV_ARGS(&resource))))
    {
        SetError(errorMessage, "Failed to allocate CBT topology buffer");
        return false;
    }
    return true;
}

// PSO 和 root signature 成对创建；shader 绑定若与签名不符会在此处直接失败。
bool CreateComputePipeline(
    ID3D12Device* device,
    ID3D12RootSignature* rootSignature,
    const std::filesystem::path& shaderPath,
    ComPtr<ID3D12PipelineState>& pipeline,
    std::string* errorMessage)
{
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
        SetError(errorMessage, "Failed to create CBT topology compute pipeline: " + shaderPath.string());
        return false;
    }
    return true;
}

// 使用版本 1.0 root signature，兼容项目当前的最低 D3D12 环境。
// 序列化器的诊断文本比 HRESULT 更有价值，因此原样转入初始化错误。
bool CreateRootSignature(
    ID3D12Device* device,
    const D3D12_ROOT_SIGNATURE_DESC& description,
    ComPtr<ID3D12RootSignature>& rootSignature,
    std::string* errorMessage)
{
    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> errors;
    HRESULT result = D3D12SerializeRootSignature(
        &description,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serialized,
        &errors);
    if (FAILED(result))
    {
        const char* detail = errors != nullptr
            ? static_cast<const char*>(errors->GetBufferPointer())
            : "unknown error";
        SetError(errorMessage, std::string{"Failed to serialize CBT topology root signature: "} + detail);
        return false;
    }
    result = device->CreateRootSignature(
        0U,
        serialized->GetBufferPointer(),
        serialized->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature));
    if (FAILED(result))
    {
        SetError(errorMessage, "Failed to create CBT topology root signature");
        return false;
    }
    return true;
}

// trackedState 是资源状态的单一 CPU 镜像，避免重复 barrier。
// E0 只在同一 direct command list 中记录，不需要跨 queue ownership 转移。
void Transition(
    ID3D12GraphicsCommandList* commandList,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES& trackedState,
    D3D12_RESOURCE_STATES nextState)
{
    if (resource == nullptr || trackedState == nextState)
    {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = trackedState;
    barrier.Transition.StateAfter = nextState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1U, &barrier);
    trackedState = nextState;
}

// 空资源 UAV barrier 表示所有先前 UAV 写入对后续 pass 可见。
// Reduce 的中间阶段使用定向 tree barrier，表达真正的数据依赖。
void UavBarrier(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource = nullptr)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    commandList->ResourceBarrier(1U, &barrier);
}

// Bootstrap 使用 root constants，避免为四个只读标量再分配 CBV。
// BaseElementOffset 把固定六个根二分器定位到完整 heap-id 数组尾部。
struct BootstrapConstants
{
    std::uint32_t TotalElementCount{0U};
    std::uint32_t BaseElementOffset{0U};
    std::uint32_t BaseElementCount{0U};
    float TerrainSize{0.0F};
    std::uint32_t BaseDepth{0U};
};

struct TopologyUpdateConstants
{
    std::array<float, 4> AreaAndScreen{};
    std::array<float, 4> CameraPosition{};
    std::array<float, 4> CameraForward{};
    std::array<std::array<float, 4>, 6> FrustumPlanes{};
};

static_assert(sizeof(BootstrapConstants) == 20U);
static_assert(sizeof(TopologyUpdateConstants) == 144U);
static_assert(sizeof(Terrain::TerrainMeshVertex) == 52U);
static_assert(E3DiagnosticReadbackBytes == 68U);
static_assert(E3ValidationReadbackBytes == 304U);
} // namespace

D3D12CbtE0Pipeline::~D3D12CbtE0Pipeline()
{
    Shutdown();
}

bool D3D12CbtE0Pipeline::Initialize(
    Render::D3D12GraphicsBackend& backend,
    const CbtBaseTopology& topology,
    const D3D12CbtTopologyResourceView& resources,
    std::string* errorMessage)
{
    // 初始化采用全有或全无语义：任一资源或 PSO 失败都会释放此前创建的对象。
    // Topology 本身由算法状态拥有，本对象只借用它来创建视图并记录命令。
    Shutdown();
    _backend = &backend;
    _capacity = topology.Layout.Occupancy.Capacity;
    if (!CreateTopologyRootSignature(errorMessage) ||
        !CreateBootstrapRootSignature(errorMessage) ||
        !CreateDispatchCommandSignature(errorMessage) ||
        !CreatePipelines(errorMessage) ||
        !CreateConstantBuffers(errorMessage) ||
        !CreateClassificationReadbacks(errorMessage) ||
        !CreateGeometryBuffers(topology, errorMessage) ||
        !ConfigureTopologyDescriptors(topology, resources, errorMessage))
    {
        Shutdown();
        return false;
    }
    _initialized = true;
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    return true;
}

void D3D12CbtE0Pipeline::Shutdown()
{
    // Frame CB 持久映射；释放资源前必须逐一解除映射。
    for (std::size_t frame = 0U; frame < _constantBuffers.size(); ++frame)
    {
        if (_constantBuffers[frame] != nullptr && _mappedConstants[frame] != nullptr)
        {
            _constantBuffers[frame]->Unmap(0U, nullptr);
        }
        _mappedConstants[frame] = nullptr;
        _constantBuffers[frame].Reset();
    }
    if (_backend != nullptr)
    {
        // 十七个 UAV 是一个连续分配，必须按原始 range 一次归还。
        _backend->ReleaseSrvDescriptor(_topologySrvRange);
        _backend->ReleaseSrvDescriptor(_topologyUavRange);
    }
    _classificationReadbacks = {};
    _classificationReadbackPending = {};
    _classificationReadbackGenerations = {};
    _classificationValidationPending = {};
    _classificationExactReferencePending = {};
    _expectedSplitCandidateCounts = {};
    _expectedSimplifyCandidateCounts = {};
    _expectedSubdivisionPatterns = {};
    _expectedPlannedSplitNodeCounts = {};
    _expectedAllocatedSplitSlotCounts = {};
    _expectedRemainingDynamicSlotCounts = {};
    _renderVertices.Reset();
    _classificationPositions.Reset();
    _renderVertexCapacityBytes = 0U;
    _classificationPositionCapacityBytes = 0U;
    _modifiedGeometryPipeline.Reset();
    _baseGeometryPipeline.Reset();
    _prepareIndirectPipeline.Reset();
    _indexationPipeline.Reset();
    _capacityPipelines = {};
    _dispatchCommandSignature.Reset();
    _bootstrapRootSignature.Reset();
    _topologyRootSignature.Reset();
    _topologyFrameGeneration = 0U;
    _classificationSampleGeneration = 0U;
    _lastSplitCandidateCount = 0U;
    _lastSimplifyCandidateCount = 0U;
    _lastPlannedSplitNodeCount = 0U;
    _lastAllocatedSplitSlotCount = 0U;
    _lastRemainingDynamicSlotCount = 0U;
    _lastDuplicateSplitClaimCount = 0U;
    _lastSharedCompatibilityCount = 0U;
    _lastCompatibilityStepCount = 0U;
    _lastMaximumCompatibilityLength = 0U;
    _lastCommittedDynamicSlotCount = 0U;
    _lastSplitPropagationCount = 0U;
    _lastBisectTemplateCounts = {};
    _lastActiveDynamicSlotCount = 0U;
    _lastIndexedActiveCount = CbtBaseBisectorCount;
    _neighborReadIndex = 0U;
    _heapIdState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _bisectorDataState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _baseControlPointState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _activeIndexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _visibleIndexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _modifiedIndexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _classificationState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _allocationState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _memoryState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _validationState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _occupancyTreeState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _topologyDispatchState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _drawState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _geometryDispatchState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _classificationPositionState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    _renderVertexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    // 不缓存外部 backend 指针，销毁后对象可安全地重新初始化。
    _initialized = false;
    _backend = nullptr;
}

bool D3D12CbtE0Pipeline::CreateTopologyRootSignature(std::string* errorMessage)
{
    // 生产 topology 签名严格对应 CbtTopologyE0.hlsl：
    // root[0] = b0，全局/视图常量。
    // root[1] = b1，几何和容量常量。
    // root[2] = b2，逐帧更新常量。
    // root[3] = t0..t1，分类位置和上一帧活动索引。
    // root[4] = u0..u16，完整 CBT 可写资源表。
    // root[3] 只由 Classify 消费，Reset/Reduce 不访问该表。
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 2U;
    srvRange.BaseShaderRegister = 0U;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = TopologyUavCount;
    uavRange.BaseShaderRegister = 0U;
    uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    std::array<D3D12_ROOT_PARAMETER, 5> parameters{};
    for (std::uint32_t index = 0U; index < 3U; ++index)
    {
        parameters[index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[index].Descriptor.ShaderRegister = index;
        parameters[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[3].DescriptorTable.NumDescriptorRanges = 1U;
    parameters[3].DescriptorTable.pDescriptorRanges = &srvRange;
    parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[4].DescriptorTable.NumDescriptorRanges = 1U;
    parameters[4].DescriptorTable.pDescriptorRanges = &uavRange;

    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = static_cast<UINT>(parameters.size());
    description.pParameters = parameters.data();
    return CreateRootSignature(_backend->Device(), description, _topologyRootSignature, errorMessage);
}

bool D3D12CbtE0Pipeline::CreateBootstrapRootSignature(std::string* errorMessage)
{
    // Bootstrap 是仓库侧适配层，使用 root descriptors 直接借用常驻资源。
    // root[0] 提供四个 32 位常量。
    // root[1..3] 分别为 heap ids、bisector data、base control points。
    // root[4..10] 分别为三类索引、draw state、geometry dispatch 和两类几何输出。
    // 该签名不进入生产 topology ABI，因此不会改变官方 u0..u16 编号。
    std::array<D3D12_ROOT_PARAMETER, 11> parameters{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.Num32BitValues = 5U;
    parameters[0].Constants.ShaderRegister = 0U;
    for (std::uint32_t index = 0U; index < 3U; ++index)
    {
        parameters[1U + index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[1U + index].Descriptor.ShaderRegister = index;
    }
    for (std::uint32_t index = 0U; index < 7U; ++index)
    {
        parameters[4U + index].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        parameters[4U + index].Descriptor.ShaderRegister = index;
    }

    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = static_cast<UINT>(parameters.size());
    description.pParameters = parameters.data();
    return CreateRootSignature(_backend->Device(), description, _bootstrapRootSignature, errorMessage);
}

bool D3D12CbtE0Pipeline::CreateDispatchCommandSignature(std::string* errorMessage)
{
    D3D12_INDIRECT_ARGUMENT_DESC argument{};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
    D3D12_COMMAND_SIGNATURE_DESC description{};
    description.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
    description.NumArgumentDescs = 1U;
    description.pArgumentDescs = &argument;
    if (FAILED(_backend->Device()->CreateCommandSignature(
            &description,
            nullptr,
            IID_PPV_ARGS(&_dispatchCommandSignature))))
    {
        SetError(errorMessage, "Failed to create CBT E3 dispatch command signature");
        return false;
    }
    return true;
}

bool D3D12CbtE0Pipeline::CreatePipelines(std::string* errorMessage)
{
#if defined(PARALLEL_ROAM_DX12_SHADER_DIR)
    const std::filesystem::path shaderDirectory{PARALLEL_ROAM_DX12_SHADER_DIR};
#else
    const std::filesystem::path shaderDirectory{"assets/shaders/dx12"};
#endif
    const std::array<const char*, 4> capacityNames{"128K", "256K", "512K", "1M"};
    // OCBT reduction 的数组长度由编译期宏决定，不能用一个动态 PSO 混用容量。
    // 每档包含 E0-E3 分类、规划、提交、传播、Reduce 和验证生产管线。
    for (std::size_t index = 0U; index < capacityNames.size(); ++index)
    {
        const std::string prefix = std::string{"CbtTopology"} + capacityNames[index];
        CapacityPipelines& pipelines = _capacityPipelines[index];
        if (!CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "ResetE0.cso"), pipelines.Reset, errorMessage) ||
            !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "Classify.cso"), pipelines.Classify, errorMessage) ||
            !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "PrepareClassificationIndirect.cso"), pipelines.PrepareClassificationIndirect, errorMessage) ||
            !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "SplitE2.cso"), pipelines.Split, errorMessage) ||
            !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "PrepareAllocationIndirect.cso"), pipelines.PrepareAllocationIndirect, errorMessage) ||
            !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "AllocateE2.cso"), pipelines.Allocate, errorMessage) ||
            !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "CopyNeighborsE3.cso"), pipelines.CopyNeighbors, errorMessage) ||
            !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "BisectE3.cso"), pipelines.Bisect, errorMessage) ||
            !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "PreparePropagationIndirectE3.cso"), pipelines.PreparePropagationIndirect, errorMessage) ||
            !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "PropagateBisectE3.cso"), pipelines.PropagateBisect, errorMessage) ||
            !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "ReducePre.cso"), pipelines.ReducePre, errorMessage) ||
            !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "ReduceFirst.cso"), pipelines.ReduceFirst, errorMessage) ||
            !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "ReduceSecond.cso"), pipelines.ReduceSecond, errorMessage) ||
            !CreateComputePipeline(_backend->Device(), _topologyRootSignature.Get(), shaderDirectory / (prefix + "ValidateE3.cso"), pipelines.Validate, errorMessage))
        {
            return false;
        }
    }

    // Bootstrap PSO 与容量无关，边界由 root constants 在运行时提供。
    return CreateComputePipeline(_backend->Device(), _bootstrapRootSignature.Get(), shaderDirectory / "CbtBootstrapIndexation.cso", _indexationPipeline, errorMessage) &&
           CreateComputePipeline(_backend->Device(), _bootstrapRootSignature.Get(), shaderDirectory / "CbtBootstrapPrepareIndirect.cso", _prepareIndirectPipeline, errorMessage) &&
           CreateComputePipeline(_backend->Device(), _bootstrapRootSignature.Get(), shaderDirectory / "CbtBootstrapBuildBaseGeometry.cso", _baseGeometryPipeline, errorMessage) &&
           CreateComputePipeline(_backend->Device(), _bootstrapRootSignature.Get(), shaderDirectory / "CbtBootstrapBuildModifiedGeometry.cso", _modifiedGeometryPipeline, errorMessage);
}

bool D3D12CbtE0Pipeline::CreateConstantBuffers(std::string* errorMessage)
{
    // 一个 frame resource 对应一块 upload buffer，生命周期与 swap-chain frame 同步。
    for (std::size_t frame = 0U; frame < _constantBuffers.size(); ++frame)
    {
        if (!CreateBuffer(
                _backend->Device(),
                D3D12_HEAP_TYPE_UPLOAD,
                ConstantBufferBytes,
                D3D12_RESOURCE_FLAG_NONE,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                _constantBuffers[frame],
                errorMessage))
        {
            return false;
        }
        const D3D12_RANGE noRead{0U, 0U};
        // CPU 只写这些常量，空 read range 避免驱动为 CPU 读访问做额外同步。
        void* mapped = nullptr;
        if (FAILED(_constantBuffers[frame]->Map(0U, &noRead, &mapped)))
        {
            SetError(errorMessage, "Failed to map CBT topology frame constants");
            return false;
        }
        _mappedConstants[frame] = static_cast<std::uint8_t*>(mapped);
    }
    return true;
}

bool D3D12CbtE0Pipeline::CreateClassificationReadbacks(std::string* errorMessage)
{
    for (Microsoft::WRL::ComPtr<ID3D12Resource>& readback : _classificationReadbacks)
    {
        if (!CreateBuffer(
                _backend->Device(),
                D3D12_HEAP_TYPE_READBACK,
                E3ValidationReadbackBytes,
                D3D12_RESOURCE_FLAG_NONE,
                D3D12_RESOURCE_STATE_COPY_DEST,
                readback,
                errorMessage))
        {
            return false;
        }
    }
    return true;
}

bool D3D12CbtE0Pipeline::CreateGeometryBuffers(
    const CbtBaseTopology& topology,
    std::string* errorMessage)
{
    // Classification position 使用上游四点布局：每个潜在二分器四个 float3。
    // Render vertex 使用仓库 52-byte TerrainMeshVertex，且每个活动槽预留三个顶点。
    // 两者都按总 element capacity 分配，后续 split 不需要替换原生资源地址。
    _classificationPositionCapacityBytes =
        static_cast<std::size_t>(topology.Layout.TotalElementCount) * 4U * sizeof(float) * 3U;
    _renderVertexCapacityBytes =
        static_cast<std::size_t>(topology.Layout.TotalElementCount) * 3U * sizeof(Terrain::TerrainMeshVertex);
    return CreateBuffer(
               _backend->Device(),
               D3D12_HEAP_TYPE_DEFAULT,
               _classificationPositionCapacityBytes,
               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               _classificationPositions,
               errorMessage) &&
           CreateBuffer(
               _backend->Device(),
               D3D12_HEAP_TYPE_DEFAULT,
               _renderVertexCapacityBytes,
               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
               _renderVertices,
               errorMessage);
}

bool D3D12CbtE0Pipeline::ConfigureTopologyDescriptors(
    const CbtBaseTopology& topology,
    const D3D12CbtTopologyResourceView& resources,
    std::string* errorMessage)
{
    // 两个 SRV 和十七个 UAV 分别连续 因为各 root table 只保存首个 GPU handle
    _topologySrvRange = _backend->AllocateSrvDescriptorRange(2U);
    if (!_topologySrvRange.IsValid())
    {
        SetError(errorMessage, "D3D12 descriptor heap has no contiguous 2-SRV range for CBT");
        return false;
    }
    _topologyUavRange = _backend->AllocateSrvDescriptorRange(TopologyUavCount);
    if (!_topologyUavRange.IsValid())
    {
        SetError(errorMessage, "D3D12 descriptor heap has no contiguous 17-UAV range for CBT");
        return false;
    }

    struct UavBinding
    {
        ID3D12Resource* Resource;
        std::uint32_t ElementCount;
        std::uint32_t Stride;
    };
    // 下列顺序是生产 shader ABI，不允许按 C++ 使用频率重排：
    // u0  occupancy tree，保存各层前缀计数。
    // u1  occupancy bitfield，保存 leaf occupancy 位。
    // u2  heap ids，连接稠密 element slot 与二叉 heap id。
    // u3  neighbor ping，当前邻接关系输入。
    // u4  neighbor pong，下一轮邻接关系输出。
    // u5  bisector data，严格 32 字节的拓扑记录。
    // u6  classification，分类结果或任务标记。
    // u7  simplification，merge 任务暂存。
    // u8  allocation，split 槽位分配结果。
    // u9  propagation，邻接传播任务。
    // u10 memory，活动数与容量计数器。
    // u11 topology dispatch，三组 D3D12_DISPATCH_ARGUMENTS。
    // u12 draw state，以 D3D12_DRAW_ARGUMENTS 开头的十字协议。
    // u13 active indices，所有活动二分器的稠密槽位。
    // u14 visible indices，通过可见性筛选的槽位。
    // u15 modified indices，本帧需要重建几何的槽位。
    // u16 validation，错误码和首个问题元素。
    const CbtTopologyBufferLayout& layout = topology.Layout;
    const std::array<UavBinding, TopologyUavCount> bindings{{
        {resources.OccupancyTree, layout.Occupancy.TreeSlotCount, sizeof(std::uint32_t)},
        {resources.OccupancyBitfield, layout.Occupancy.BitfieldSlotCount, sizeof(std::uint64_t)},
        {resources.HeapIds, layout.TotalElementCount, sizeof(std::uint64_t)},
        {resources.Neighbors[0], layout.TotalElementCount, sizeof(CbtBisectorNeighbors)},
        {resources.Neighbors[1], layout.TotalElementCount, sizeof(CbtBisectorNeighbors)},
        {resources.BisectorData, layout.TotalElementCount, sizeof(CbtBisectorData)},
        {resources.Classification, layout.ClassificationElementCount, sizeof(std::uint32_t)},
        {resources.Simplification, layout.SimplificationElementCount, sizeof(std::uint32_t)},
        {resources.Allocation, layout.AllocationElementCount, sizeof(std::int32_t)},
        {resources.Propagation, layout.PropagationElementCount, sizeof(std::int32_t)},
        {resources.Memory, layout.MemoryElementCount, sizeof(std::int32_t)},
        {resources.TopologyDispatchCommands, layout.TopologyDispatchElementCount, sizeof(std::uint32_t)},
        {resources.IndirectDrawState, layout.DrawStateElementCount, sizeof(std::uint32_t)},
        {resources.ActiveIndices, layout.IndexElementCount, sizeof(std::uint32_t)},
        {resources.VisibleIndices, layout.IndexElementCount, sizeof(std::uint32_t)},
        {resources.ModifiedIndices, layout.IndexElementCount, sizeof(std::uint32_t)},
        {resources.Validation, layout.ValidationElementCount, sizeof(std::uint32_t)},
    }};

    const UINT descriptorSize = _backend->Device()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (std::uint32_t index = 0U; index < TopologyUavCount; ++index)
    {
        // 任一空资源都会使 descriptor table 部分有效，必须在创建视图前拒绝。
        if (bindings[index].Resource == nullptr)
        {
            SetError(errorMessage, "CBT E3 topology resource view is incomplete");
            return false;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC description{};
        description.Format = DXGI_FORMAT_UNKNOWN;
        description.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        description.Buffer.NumElements = bindings[index].ElementCount;
        description.Buffer.StructureByteStride = bindings[index].Stride;
        // range 的 CPU handle 按设备给出的增量推进，不能假设描述符字节大小。
        D3D12_CPU_DESCRIPTOR_HANDLE handle = _topologyUavRange.Cpu;
        handle.ptr += static_cast<SIZE_T>(index) * descriptorSize;
        _backend->Device()->CreateUnorderedAccessView(
            bindings[index].Resource,
            nullptr,
            &description,
            handle);
    }

    const std::array<UavBinding, 2> srvBindings{{
        {_classificationPositions.Get(), layout.TotalElementCount * 4U, sizeof(float) * 3U},
        {resources.ActiveIndices, layout.IndexElementCount, sizeof(std::uint32_t)},
    }};
    for (std::uint32_t index = 0U; index < srvBindings.size(); ++index)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC description{};
        description.Format = DXGI_FORMAT_UNKNOWN;
        description.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        description.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        description.Buffer.NumElements = srvBindings[index].ElementCount;
        description.Buffer.StructureByteStride = srvBindings[index].Stride;
        D3D12_CPU_DESCRIPTOR_HANDLE handle = _topologySrvRange.Cpu;
        handle.ptr += static_cast<SIZE_T>(index) * descriptorSize;
        _backend->Device()->CreateShaderResourceView(srvBindings[index].Resource, &description, handle);
    }
    return true;
}

bool D3D12CbtE0Pipeline::ReadCompletedClassification(
    std::uint32_t frameIndex,
    std::string* errorMessage)
{
    if (!_classificationReadbackPending[frameIndex])
    {
        return true;
    }

    const std::size_t readbackBytes = _classificationValidationPending[frameIndex]
        ? E3ValidationReadbackBytes
        : E3DiagnosticReadbackBytes;
    const D3D12_RANGE readRange{0U, readbackBytes};
    void* mapped = nullptr;
    if (FAILED(_classificationReadbacks[frameIndex]->Map(0U, &readRange, &mapped)))
    {
        SetError(errorMessage, "Failed to map completed CBT E3 diagnostic counters");
        return false;
    }
    const auto* bytes = static_cast<const std::uint8_t*>(mapped);
    const auto* counters = reinterpret_cast<const std::uint32_t*>(bytes + ClassificationCounterOffset);
    _lastSplitCandidateCount = counters[0];
    _lastSimplifyCandidateCount = counters[1];
    std::memcpy(
        &_lastPlannedSplitNodeCount,
        bytes + AllocationCounterOffset,
        sizeof(_lastPlannedSplitNodeCount));
    const auto* memoryCounters = reinterpret_cast<const std::uint32_t*>(bytes + MemoryCounterOffset);
    _lastAllocatedSplitSlotCount = memoryCounters[0];
    _lastRemainingDynamicSlotCount = memoryCounters[1];
    std::array<std::uint32_t, 12> validation{};
    std::memcpy(validation.data(), bytes + ValidationCounterOffset, sizeof(validation));
    _lastDuplicateSplitClaimCount = validation[2];
    _lastSharedCompatibilityCount = validation[3];
    _lastCompatibilityStepCount = validation[4];
    _lastMaximumCompatibilityLength = validation[5];
    _lastCommittedDynamicSlotCount = validation[6];
    _lastSplitPropagationCount = validation[7];
    std::copy_n(validation.begin() + 8, _lastBisectTemplateCounts.size(), _lastBisectTemplateCounts.begin());
    const D3D12_RANGE noWrite{0U, 0U};
    _classificationSampleGeneration = _classificationReadbackGenerations[frameIndex];
    _classificationReadbackPending[frameIndex] = false;
    if (validation[0] != 0U)
    {
        SetError(
            errorMessage,
            "CBT E3 shader validation failed at generation " +
                std::to_string(_classificationSampleGeneration) + ": code/slot=" +
                std::to_string(validation[0]) + "/" + std::to_string(validation[1]));
        _classificationReadbacks[frameIndex]->Unmap(0U, &noWrite);
        return false;
    }
    if (_classificationValidationPending[frameIndex])
    {
        _classificationValidationPending[frameIndex] = false;
        std::uint32_t occupancyRoot = 0U;
        CbtDrawState drawState{};
        std::memcpy(&occupancyRoot, bytes + OccupancyRootOffset, sizeof(occupancyRoot));
        std::memcpy(&drawState, bytes + DrawStateReadbackOffset, sizeof(drawState));
        _lastActiveDynamicSlotCount = occupancyRoot;
        _lastIndexedActiveCount = drawState.ActiveBisectorCount;
        if (_lastCommittedDynamicSlotCount != _lastAllocatedSplitSlotCount ||
            drawState.Active.VertexCountPerInstance / 3U != drawState.ActiveBisectorCount ||
            drawState.ActiveBisectorCount != occupancyRoot + CbtBaseBisectorCount ||
            drawState.Visible.VertexCountPerInstance > drawState.Active.VertexCountPerInstance ||
            drawState.ModifiedPositionCount / 4U > drawState.ActiveBisectorCount)
        {
            SetError(
                errorMessage,
                "CBT E3 occupancy/indexation mismatch at generation " +
                    std::to_string(_classificationSampleGeneration));
            _classificationReadbacks[frameIndex]->Unmap(0U, &noWrite);
            return false;
        }

        const bool exactReference = _classificationExactReferencePending[frameIndex];
        _classificationExactReferencePending[frameIndex] = false;
        if (exactReference)
        {
        const std::uint32_t expectedSplit = _expectedSplitCandidateCounts[frameIndex];
        const std::uint32_t expectedSimplify = _expectedSimplifyCandidateCounts[frameIndex];
        if (_lastSplitCandidateCount != expectedSplit ||
            _lastSimplifyCandidateCount != expectedSimplify)
        {
            SetError(
                errorMessage,
                "CBT E1 CPU/GPU classification mismatch at generation " +
                    std::to_string(_classificationSampleGeneration) +
                    ": expected split/simplify=" + std::to_string(expectedSplit) + "/" +
                    std::to_string(expectedSimplify) + ", GPU=" +
                    std::to_string(_lastSplitCandidateCount) + "/" +
                    std::to_string(_lastSimplifyCandidateCount));
            _classificationReadbacks[frameIndex]->Unmap(0U, &noWrite);
            return false;
        }

        const std::uint32_t expectedPlanNodes = _expectedPlannedSplitNodeCounts[frameIndex];
        const std::uint32_t expectedSlots = _expectedAllocatedSplitSlotCounts[frameIndex];
        const std::uint32_t expectedRemaining = _expectedRemainingDynamicSlotCounts[frameIndex];
        if (_lastPlannedSplitNodeCount != expectedPlanNodes ||
            _lastAllocatedSplitSlotCount != expectedSlots ||
            _lastRemainingDynamicSlotCount != expectedRemaining)
        {
            SetError(
                errorMessage,
                "CBT E2 planning counter mismatch at generation " +
                    std::to_string(_classificationSampleGeneration) +
                    ": expected nodes/slots/remaining=" + std::to_string(expectedPlanNodes) + "/" +
                    std::to_string(expectedSlots) + "/" + std::to_string(expectedRemaining) +
                    ", GPU=" + std::to_string(_lastPlannedSplitNodeCount) + "/" +
                    std::to_string(_lastAllocatedSplitSlotCount) + "/" +
                    std::to_string(_lastRemainingDynamicSlotCount));
            _classificationReadbacks[frameIndex]->Unmap(0U, &noWrite);
            return false;
        }

        std::array<CbtBisectorData, CbtBaseBisectorCount> baseData{};
        std::memcpy(baseData.data(), bytes + BaseBisectorDataOffset, sizeof(baseData));
        std::vector<std::uint32_t> allocatedSlots;
        for (std::size_t node = 0U; node < baseData.size(); ++node)
        {
            const std::uint32_t expectedPattern = _expectedSubdivisionPatterns[frameIndex][node];
            if (baseData[node].SubdivisionPattern != expectedPattern)
            {
                SetError(
                    errorMessage,
                    "CBT E2 subdivision pattern mismatch at generation " +
                        std::to_string(_classificationSampleGeneration) + ", base node=" +
                        std::to_string(node) + ", expected=" + std::to_string(expectedPattern) +
                        ", GPU=" + std::to_string(baseData[node].SubdivisionPattern));
                _classificationReadbacks[frameIndex]->Unmap(0U, &noWrite);
                return false;
            }
            const std::uint32_t slotCount = static_cast<std::uint32_t>(std::popcount(expectedPattern));
            for (std::uint32_t slot = 0U; slot < slotCount; ++slot)
            {
                allocatedSlots.push_back(baseData[node].Indices[slot]);
            }
        }
        std::sort(allocatedSlots.begin(), allocatedSlots.end());
        if (allocatedSlots.size() != expectedSlots ||
            std::adjacent_find(allocatedSlots.begin(), allocatedSlots.end()) != allocatedSlots.end())
        {
            SetError(errorMessage, "CBT E2 allocated physical slots are incomplete or duplicated");
            _classificationReadbacks[frameIndex]->Unmap(0U, &noWrite);
            return false;
        }
        for (std::uint32_t rank = 0U; rank < allocatedSlots.size(); ++rank)
        {
            // E2 尚未提交任何动态位，所以旧 OCBT 的前 k 个 free rank 必须恰好解码为 0..k-1。
            if (allocatedSlots[rank] != rank)
            {
                SetError(
                    errorMessage,
                    "CBT E2 allocation did not use the old OCBT complement at free rank " +
                        std::to_string(rank));
                _classificationReadbacks[frameIndex]->Unmap(0U, &noWrite);
                return false;
            }
        }

        if (occupancyRoot != expectedSlots)
        {
            SetError(
                errorMessage,
                "CBT E3 committed OCBT root mismatch: expected=" +
                    std::to_string(expectedSlots) + ", GPU=" + std::to_string(occupancyRoot));
            _classificationReadbacks[frameIndex]->Unmap(0U, &noWrite);
            return false;
        }
        }
    }
    _classificationReadbacks[frameIndex]->Unmap(0U, &noWrite);
    return true;
}

bool D3D12CbtE0Pipeline::RecordFrame(
    const TerrainLodBuildInput& input,
    const CbtBaseTopology& topology,
    const D3D12CbtTopologyResourceView& resources,
    bool rebuildGeometry,
    std::string* errorMessage)
{
    if (!_initialized || _backend == nullptr || !_backend->FrameOpen() || _backend->CommandList() == nullptr)
    {
        SetError(errorMessage, "CBT E3 requires an initialized pipeline and an open D3D12 frame");
        return false;
    }
    ID3D12GraphicsCommandList* commandList = _backend->CommandList();
    const std::uint32_t frameIndex = _backend->CurrentFrameIndex();
    if (!ReadCompletedClassification(frameIndex, errorMessage))
    {
        return false;
    }

    // 验证值与本帧 readback 槽绑定；等该 swap-chain 槽完成并再次复用时再比较，
    // 因而不会为统计或验证额外等待 GPU。
    std::uint32_t expectedSplitCandidateCount = 0U;
    std::uint32_t expectedSimplifyCandidateCount = 0U;
    std::array<std::uint32_t, CbtBaseBisectorCount> expectedSubdivisionPatterns{};
    std::uint32_t expectedPlannedSplitNodeCount = 0U;
    std::uint32_t expectedAllocatedSplitSlotCount = 0U;
    std::uint32_t expectedRemainingDynamicSlotCount = 0U;
    const bool exactReference =
        input.Settings.EnableTopologyValidation && _topologyFrameGeneration == 0U;
    if (exactReference)
    {
        const auto triangles = BuildCbtBaseClassificationTriangles(topology, input.Settings.TerrainSize);
        const std::uint32_t maxDepth = static_cast<std::uint32_t>(std::max(input.Settings.MaxDepth, 0));
        std::array<CbtClassificationResult, CbtBaseBisectorCount> classificationResults{};
        std::vector<std::uint32_t> splitCandidates;
        for (std::size_t node = 0U; node < triangles.size(); ++node)
        {
            const CbtClassificationResult result = EvaluateCbtClassification(
                triangles[node],
                input.View,
                input.Settings.CbtTriangleAreaPixels,
                topology.BaseDepth,
                maxDepth).Result;
            classificationResults[node] = result;
            expectedSplitCandidateCount += result == CbtClassificationResult::Bisect ? 1U : 0U;
            if (result == CbtClassificationResult::Bisect)
            {
                splitCandidates.push_back(static_cast<std::uint32_t>(node));
            }
        }
        // E1 只分类固定 base leaf；官方协议禁止 base depth 进入 simplify 候选表。
        expectedSimplifyCandidateCount = 0U;

        std::vector<CbtSplitPlanningNode> planningNodes(CbtBaseBisectorCount);
        const auto toLocalNeighbor = [&](std::uint32_t physicalNeighbor) {
            if (physicalNeighbor == InvalidCbtBisectorIndex)
            {
                return InvalidCbtBisectorIndex;
            }
            if (physicalNeighbor < topology.Layout.BaseElementOffset ||
                physicalNeighbor >= topology.Layout.TotalElementCount)
            {
                return InvalidCbtBisectorIndex;
            }
            return physicalNeighbor - topology.Layout.BaseElementOffset;
        };
        for (std::size_t node = 0U; node < planningNodes.size(); ++node)
        {
            const CbtBisectorNeighbors& neighbors = topology.Neighbors[node];
            planningNodes[node] = {
                topology.HeapIds[node],
                {
                    toLocalNeighbor(neighbors.Previous),
                    toLocalNeighbor(neighbors.Next),
                    toLocalNeighbor(neighbors.Twin),
                },
                classificationResults[node] == CbtClassificationResult::Bisect
                    ? static_cast<std::int32_t>(CbtClassificationResult::Bisect)
                    : static_cast<std::int32_t>(CbtClassificationResult::Unchanged),
            };
        }
        const CbtSplitPlanningResult plan = PlanCbtSplits(
            planningNodes,
            splitCandidates,
            topology.BaseDepth,
            topology.Layout.DynamicElementCount);
        if (!plan.Valid || plan.SubdivisionPatterns.size() != expectedSubdivisionPatterns.size())
        {
            SetError(errorMessage, "CBT E3 CPU split planning reference rejected the base topology");
            return false;
        }
        std::copy(
            plan.SubdivisionPatterns.begin(),
            plan.SubdivisionPatterns.end(),
            expectedSubdivisionPatterns.begin());
        expectedPlannedSplitNodeCount = static_cast<std::uint32_t>(plan.AllocationNodes.size());
        expectedAllocatedSplitSlotCount = plan.RequiredSlotCount;
        expectedRemainingDynamicSlotCount = plan.RemainingMemory;
    }

    std::memset(_mappedConstants[frameIndex], 0, ConstantBufferBytes);
    std::memcpy(_mappedConstants[frameIndex], &input.View.ViewProjection, sizeof(input.View.ViewProjection));
    const std::array<std::uint32_t, 7> geometryConstants{
        topology.Layout.TotalElementCount,
        topology.Layout.BaseElementOffset,
        CbtBaseBisectorCount,
        topology.BaseDepth,
        topology.Layout.DynamicElementCount,
        static_cast<std::uint32_t>(std::max(input.Settings.MaxDepth, 0)),
        _neighborReadIndex,
    };
    std::memcpy(
        _mappedConstants[frameIndex] + ConstantBufferSlotBytes,
        geometryConstants.data(),
        sizeof(geometryConstants));

    TopologyUpdateConstants updateConstants{};
    updateConstants.AreaAndScreen = {
        input.Settings.CbtTriangleAreaPixels,
        static_cast<float>(input.View.DrawableWidth),
        static_cast<float>(input.View.DrawableHeight),
        0.0F,
    };
    updateConstants.CameraPosition = {
        input.View.CameraPosition.x,
        input.View.CameraPosition.y,
        input.View.CameraPosition.z,
        0.0F,
    };
    updateConstants.CameraForward = {
        input.View.CameraForward.x,
        input.View.CameraForward.y,
        input.View.CameraForward.z,
        0.0F,
    };
    for (std::size_t plane = 0U; plane < updateConstants.FrustumPlanes.size(); ++plane)
    {
        const glm::vec4& source = input.View.FrustumPlanes[plane];
        updateConstants.FrustumPlanes[plane] = {source.x, source.y, source.z, source.w};
    }
    std::memcpy(
        _mappedConstants[frameIndex] + ConstantBufferSlotBytes * 2U,
        &updateConstants,
        sizeof(updateConstants));

    ID3D12DescriptorHeap* descriptorHeaps[] = {_backend->ShaderVisibleSrvHeap()};
    commandList->SetDescriptorHeaps(1U, descriptorHeaps);
    const BootstrapConstants bootstrapConstants{
        topology.Layout.TotalElementCount,
        topology.Layout.BaseElementOffset,
        CbtBaseBisectorCount,
        input.Settings.TerrainSize,
        topology.BaseDepth,
    };
    commandList->SetComputeRootSignature(_bootstrapRootSignature.Get());
    commandList->SetComputeRoot32BitConstants(0U, 5U, &bootstrapConstants, 0U);
    commandList->SetComputeRootShaderResourceView(1U, resources.HeapIds->GetGPUVirtualAddress());
    commandList->SetComputeRootShaderResourceView(2U, resources.BisectorData->GetGPUVirtualAddress());
    commandList->SetComputeRootShaderResourceView(3U, resources.BaseControlPoints->GetGPUVirtualAddress());
    commandList->SetComputeRootUnorderedAccessView(4U, resources.ActiveIndices->GetGPUVirtualAddress());
    commandList->SetComputeRootUnorderedAccessView(5U, resources.VisibleIndices->GetGPUVirtualAddress());
    commandList->SetComputeRootUnorderedAccessView(6U, resources.ModifiedIndices->GetGPUVirtualAddress());
    commandList->SetComputeRootUnorderedAccessView(7U, resources.IndirectDrawState->GetGPUVirtualAddress());
    commandList->SetComputeRootUnorderedAccessView(8U, resources.GeometryDispatchCommands->GetGPUVirtualAddress());
    commandList->SetComputeRootUnorderedAccessView(9U, _classificationPositions->GetGPUVirtualAddress());
    commandList->SetComputeRootUnorderedAccessView(10U, _renderVertices->GetGPUVirtualAddress());

    // Classify 必须看到已初始化的三子位置和父位置 因此平面 Bootstrap 位于首轮分类之前
    if (rebuildGeometry)
    {
        Transition(commandList, resources.BaseControlPoints, _baseControlPointState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(commandList, _classificationPositions.Get(), _classificationPositionState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Transition(commandList, _renderVertices.Get(), _renderVertexState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList->SetPipelineState(_baseGeometryPipeline.Get());
        commandList->Dispatch(DispatchCount(CbtBaseBisectorCount), 1U, 1U);
        UavBarrier(commandList, _classificationPositions.Get());
        UavBarrier(commandList, _renderVertices.Get());
    }
    Transition(commandList, _classificationPositions.Get(), _classificationPositionState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(commandList, _renderVertices.Get(), _renderVertexState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    Transition(commandList, resources.ActiveIndices, _activeIndexState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(commandList, resources.GeometryDispatchCommands, _geometryDispatchState, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    Transition(commandList, resources.HeapIds, _heapIdState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.BisectorData, _bisectorDataState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.Classification, _classificationState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.TopologyDispatchCommands, _topologyDispatchState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.IndirectDrawState, _drawState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    commandList->SetComputeRootSignature(_topologyRootSignature.Get());
    const D3D12_GPU_VIRTUAL_ADDRESS constantsAddress = _constantBuffers[frameIndex]->GetGPUVirtualAddress();
    commandList->SetComputeRootConstantBufferView(0U, constantsAddress);
    commandList->SetComputeRootConstantBufferView(1U, constantsAddress + ConstantBufferSlotBytes);
    commandList->SetComputeRootConstantBufferView(2U, constantsAddress + ConstantBufferSlotBytes * 2U);
    commandList->SetComputeRootDescriptorTable(3U, _topologySrvRange.Gpu);
    commandList->SetComputeRootDescriptorTable(4U, _topologyUavRange.Gpu);

    const CbtOccupancyLayout& occupancy = topology.Layout.Occupancy;
    CapacityPipelines& pipelines = _capacityPipelines[CapacityIndex(occupancy.Capacity)];
    commandList->SetPipelineState(pipelines.Reset.Get());
    commandList->Dispatch(1U, 1U, 1U);
    UavBarrier(commandList);

    // 上一帧 PrepareIndirect 的第一组 dispatch 与 drawState[9] 共同限定活动列表消费范围
    commandList->SetPipelineState(pipelines.Classify.Get());
    commandList->ExecuteIndirect(
        _dispatchCommandSignature.Get(),
        1U,
        resources.GeometryDispatchCommands,
        0U,
        nullptr,
        0U);
    UavBarrier(commandList);

    commandList->SetPipelineState(pipelines.PrepareClassificationIndirect.Get());
    commandList->Dispatch(1U, 1U, 1U);
    UavBarrier(commandList, resources.TopologyDispatchCommands);
    Transition(
        commandList,
        resources.TopologyDispatchCommands,
        _topologyDispatchState,
        D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

    // E2 Split 只规划兼容链和预留内存，不复制邻接、不写 heapID、也不修改 OCBT 位。
    commandList->SetPipelineState(pipelines.Split.Get());
    commandList->ExecuteIndirect(
        _dispatchCommandSignature.Get(),
        1U,
        resources.TopologyDispatchCommands,
        0U,
        nullptr,
        0U);
    UavBarrier(commandList);

    Transition(
        commandList,
        resources.TopologyDispatchCommands,
        _topologyDispatchState,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->SetPipelineState(pipelines.PrepareAllocationIndirect.Get());
    commandList->Dispatch(1U, 1U, 1U);
    UavBarrier(commandList, resources.TopologyDispatchCommands);
    Transition(
        commandList,
        resources.TopologyDispatchCommands,
        _topologyDispatchState,
        D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

    // Allocate 从仍未修改的旧 OCBT 补集中取得互异槽位，只写 BisectorData.indices。
    commandList->SetPipelineState(pipelines.Allocate.Get());
    commandList->ExecuteIndirect(
        _dispatchCommandSignature.Get(),
        1U,
        resources.TopologyDispatchCommands,
        sizeof(std::uint32_t) * 6U,
        nullptr,
        0U);
    UavBarrier(commandList);

    // 邻接复制覆盖完整容量，随后四模板只写下一代的局部节点。
    commandList->SetPipelineState(pipelines.CopyNeighbors.Get());
    commandList->Dispatch(DispatchCount(topology.Layout.TotalElementCount), 1U, 1U);
    UavBarrier(commandList);
    commandList->SetPipelineState(pipelines.Bisect.Get());
    commandList->ExecuteIndirect(
        _dispatchCommandSignature.Get(),
        1U,
        resources.TopologyDispatchCommands,
        sizeof(std::uint32_t) * 6U,
        nullptr,
        0U);
    UavBarrier(commandList);

    Transition(
        commandList,
        resources.TopologyDispatchCommands,
        _topologyDispatchState,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->SetPipelineState(pipelines.PreparePropagationIndirect.Get());
    commandList->Dispatch(1U, 1U, 1U);
    UavBarrier(commandList, resources.TopologyDispatchCommands);
    Transition(
        commandList,
        resources.TopologyDispatchCommands,
        _topologyDispatchState,
        D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    commandList->SetPipelineState(pipelines.PropagateBisect.Get());
    commandList->ExecuteIndirect(
        _dispatchCommandSignature.Get(),
        1U,
        resources.TopologyDispatchCommands,
        0U,
        nullptr,
        0U);
    UavBarrier(commandList);

    commandList->SetPipelineState(pipelines.ReducePre.Get());
    commandList->Dispatch(DispatchCount(occupancy.LastTreeNodeCount), 1U, 1U);
    UavBarrier(commandList, resources.OccupancyTree);
    commandList->SetPipelineState(pipelines.ReduceFirst.Get());
    commandList->Dispatch(occupancy.SubtreeCount, 1U, 1U);
    UavBarrier(commandList, resources.OccupancyTree);
    commandList->SetPipelineState(pipelines.ReduceSecond.Get());
    commandList->Dispatch(1U, 1U, 1U);
    UavBarrier(commandList, resources.OccupancyTree);

    // 先复制稳定计数；E3 全拓扑验证与 draw state 在 Indexation 后补写同一延迟槽。
    // 两种路径都延迟到该 frame index 完成复用时映射，不增加 GPU wait。
    Transition(commandList, resources.Classification, _classificationState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(commandList, resources.Allocation, _allocationState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(commandList, resources.Memory, _memoryState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(commandList, resources.Validation, _validationState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->CopyBufferRegion(
        _classificationReadbacks[frameIndex].Get(),
        ClassificationCounterOffset,
        resources.Classification,
        0U,
        sizeof(std::uint32_t) * 2U);
    commandList->CopyBufferRegion(
        _classificationReadbacks[frameIndex].Get(),
        AllocationCounterOffset,
        resources.Allocation,
        0U,
        sizeof(std::uint32_t));
    commandList->CopyBufferRegion(
        _classificationReadbacks[frameIndex].Get(),
        MemoryCounterOffset,
        resources.Memory,
        0U,
        sizeof(std::uint32_t) * 2U);
    commandList->CopyBufferRegion(
        _classificationReadbacks[frameIndex].Get(),
        ValidationCounterOffset,
        resources.Validation,
        0U,
        sizeof(std::uint32_t) * 12U);
    if (input.Settings.EnableTopologyValidation)
    {
        Transition(commandList, resources.BisectorData, _bisectorDataState, D3D12_RESOURCE_STATE_COPY_SOURCE);
        Transition(commandList, resources.OccupancyTree, _occupancyTreeState, D3D12_RESOURCE_STATE_COPY_SOURCE);
        commandList->CopyBufferRegion(
            _classificationReadbacks[frameIndex].Get(),
            BaseBisectorDataOffset,
            resources.BisectorData,
            static_cast<std::uint64_t>(topology.Layout.BaseElementOffset) * sizeof(CbtBisectorData),
            sizeof(CbtBisectorData) * CbtBaseBisectorCount);
        commandList->CopyBufferRegion(
            _classificationReadbacks[frameIndex].Get(),
            OccupancyRootOffset,
            resources.OccupancyTree,
            0U,
            sizeof(std::uint32_t));
        Transition(commandList, resources.BisectorData, _bisectorDataState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Transition(commandList, resources.OccupancyTree, _occupancyTreeState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    Transition(commandList, resources.Classification, _classificationState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.Allocation, _allocationState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.Memory, _memoryState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.Validation, _validationState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    _classificationReadbackPending[frameIndex] = true;
    _classificationReadbackGenerations[frameIndex] = _topologyFrameGeneration + 1U;
    _classificationValidationPending[frameIndex] = input.Settings.EnableTopologyValidation;
    _classificationExactReferencePending[frameIndex] = exactReference;
    _expectedSplitCandidateCounts[frameIndex] = expectedSplitCandidateCount;
    _expectedSimplifyCandidateCounts[frameIndex] = expectedSimplifyCandidateCount;
    _expectedSubdivisionPatterns[frameIndex] = expectedSubdivisionPatterns;
    _expectedPlannedSplitNodeCounts[frameIndex] = expectedPlannedSplitNodeCount;
    _expectedAllocatedSplitSlotCounts[frameIndex] = expectedAllocatedSplitSlotCount;
    _expectedRemainingDynamicSlotCounts[frameIndex] = expectedRemainingDynamicSlotCount;

    Transition(commandList, resources.ActiveIndices, _activeIndexState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.VisibleIndices, _visibleIndexState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.ModifiedIndices, _modifiedIndexState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.GeometryDispatchCommands, _geometryDispatchState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.HeapIds, _heapIdState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(commandList, resources.BisectorData, _bisectorDataState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(commandList, resources.BaseControlPoints, _baseControlPointState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    commandList->SetComputeRootSignature(_bootstrapRootSignature.Get());
    commandList->SetComputeRoot32BitConstants(0U, 5U, &bootstrapConstants, 0U);
    commandList->SetComputeRootShaderResourceView(1U, resources.HeapIds->GetGPUVirtualAddress());
    commandList->SetComputeRootShaderResourceView(2U, resources.BisectorData->GetGPUVirtualAddress());
    commandList->SetComputeRootShaderResourceView(3U, resources.BaseControlPoints->GetGPUVirtualAddress());
    commandList->SetComputeRootUnorderedAccessView(4U, resources.ActiveIndices->GetGPUVirtualAddress());
    commandList->SetComputeRootUnorderedAccessView(5U, resources.VisibleIndices->GetGPUVirtualAddress());
    commandList->SetComputeRootUnorderedAccessView(6U, resources.ModifiedIndices->GetGPUVirtualAddress());
    commandList->SetComputeRootUnorderedAccessView(7U, resources.IndirectDrawState->GetGPUVirtualAddress());
    commandList->SetComputeRootUnorderedAccessView(8U, resources.GeometryDispatchCommands->GetGPUVirtualAddress());
    commandList->SetComputeRootUnorderedAccessView(9U, _classificationPositions->GetGPUVirtualAddress());
    commandList->SetComputeRootUnorderedAccessView(10U, _renderVertices->GetGPUVirtualAddress());

    commandList->SetPipelineState(_indexationPipeline.Get());
    commandList->Dispatch(DispatchCount(topology.Layout.TotalElementCount), 1U, 1U);
    UavBarrier(commandList);
    commandList->SetPipelineState(_prepareIndirectPipeline.Get());
    commandList->Dispatch(1U, 1U, 1U);
    UavBarrier(commandList);

    // modified list 中每个节点重新解码当前 child 和父辅助点，供下一帧分类及本帧绘制使用。
    Transition(
        commandList,
        resources.GeometryDispatchCommands,
        _geometryDispatchState,
        D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    Transition(
        commandList,
        _classificationPositions.Get(),
        _classificationPositionState,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(
        commandList,
        _renderVertices.Get(),
        _renderVertexState,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->SetPipelineState(_modifiedGeometryPipeline.Get());
    commandList->ExecuteIndirect(
        _dispatchCommandSignature.Get(),
        1U,
        resources.GeometryDispatchCommands,
        sizeof(std::uint32_t) * 6U,
        nullptr,
        0U);
    UavBarrier(commandList, _classificationPositions.Get());
    UavBarrier(commandList, _renderVertices.Get());

    // 验证 pass 扫描新邻接代次，并对照 OCBT、heapID、Indexation 与 draw state。
    Transition(commandList, resources.HeapIds, _heapIdState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, resources.BisectorData, _bisectorDataState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->SetComputeRootSignature(_topologyRootSignature.Get());
    commandList->SetComputeRootConstantBufferView(0U, constantsAddress);
    commandList->SetComputeRootConstantBufferView(1U, constantsAddress + ConstantBufferSlotBytes);
    commandList->SetComputeRootConstantBufferView(2U, constantsAddress + ConstantBufferSlotBytes * 2U);
    commandList->SetComputeRootDescriptorTable(3U, _topologySrvRange.Gpu);
    commandList->SetComputeRootDescriptorTable(4U, _topologyUavRange.Gpu);
    commandList->SetPipelineState(pipelines.Validate.Get());
    commandList->Dispatch(DispatchCount(topology.Layout.TotalElementCount), 1U, 1U);
    UavBarrier(commandList, resources.Validation);

    // 早先复制的 counters 保持不变；这里用最终验证头覆盖对应片段。
    Transition(commandList, resources.Validation, _validationState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->CopyBufferRegion(
        _classificationReadbacks[frameIndex].Get(),
        ValidationCounterOffset,
        resources.Validation,
        0U,
        sizeof(std::uint32_t) * 12U);
    Transition(commandList, resources.Validation, _validationState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (input.Settings.EnableTopologyValidation)
    {
        Transition(commandList, resources.IndirectDrawState, _drawState, D3D12_RESOURCE_STATE_COPY_SOURCE);
        commandList->CopyBufferRegion(
            _classificationReadbacks[frameIndex].Get(),
            DrawStateReadbackOffset,
            resources.IndirectDrawState,
            0U,
            sizeof(CbtDrawState));
        Transition(commandList, resources.IndirectDrawState, _drawState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    Transition(commandList, resources.ActiveIndices, _activeIndexState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(commandList, resources.VisibleIndices, _visibleIndexState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(commandList, resources.ModifiedIndices, _modifiedIndexState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(commandList, resources.IndirectDrawState, _drawState, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    Transition(commandList, resources.GeometryDispatchCommands, _geometryDispatchState, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    Transition(commandList, _classificationPositions.Get(), _classificationPositionState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(commandList, _renderVertices.Get(), _renderVertexState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    // 下一帧的常量会把刚写完的 ping/pong 代次作为只读输入。
    _neighborReadIndex ^= 1U;
    ++_topologyFrameGeneration;
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    return true;
}

ID3D12Resource* D3D12CbtE0Pipeline::RenderVertices() const
{
    return _renderVertices.Get();
}

ID3D12Resource* D3D12CbtE0Pipeline::ClassificationPositions() const
{
    return _classificationPositions.Get();
}

std::size_t D3D12CbtE0Pipeline::RenderVertexCapacityBytes() const
{
    return _renderVertexCapacityBytes;
}

std::size_t D3D12CbtE0Pipeline::ClassificationPositionCapacityBytes() const
{
    return _classificationPositionCapacityBytes;
}

std::uint64_t D3D12CbtE0Pipeline::TopologyFrameGeneration() const
{
    return _topologyFrameGeneration;
}

std::uint32_t D3D12CbtE0Pipeline::LastSplitCandidateCount() const
{
    return _lastSplitCandidateCount;
}

std::uint32_t D3D12CbtE0Pipeline::LastSimplifyCandidateCount() const
{
    return _lastSimplifyCandidateCount;
}

std::uint32_t D3D12CbtE0Pipeline::LastPlannedSplitNodeCount() const
{
    return _lastPlannedSplitNodeCount;
}

std::uint32_t D3D12CbtE0Pipeline::LastAllocatedSplitSlotCount() const
{
    return _lastAllocatedSplitSlotCount;
}

std::uint32_t D3D12CbtE0Pipeline::LastRemainingDynamicSlotCount() const
{
    return _lastRemainingDynamicSlotCount;
}

std::uint32_t D3D12CbtE0Pipeline::LastDuplicateSplitClaimCount() const
{
    return _lastDuplicateSplitClaimCount;
}

std::uint32_t D3D12CbtE0Pipeline::LastSharedCompatibilityCount() const
{
    return _lastSharedCompatibilityCount;
}

std::uint32_t D3D12CbtE0Pipeline::LastCompatibilityStepCount() const
{
    return _lastCompatibilityStepCount;
}

std::uint32_t D3D12CbtE0Pipeline::LastMaximumCompatibilityLength() const
{
    return _lastMaximumCompatibilityLength;
}

std::uint32_t D3D12CbtE0Pipeline::LastCommittedDynamicSlotCount() const
{
    return _lastCommittedDynamicSlotCount;
}

std::uint32_t D3D12CbtE0Pipeline::LastSplitPropagationCount() const
{
    return _lastSplitPropagationCount;
}

const std::array<std::uint32_t, 4>& D3D12CbtE0Pipeline::LastBisectTemplateCounts() const
{
    return _lastBisectTemplateCounts;
}

std::uint32_t D3D12CbtE0Pipeline::LastActiveDynamicSlotCount() const
{
    return _lastActiveDynamicSlotCount;
}

std::uint32_t D3D12CbtE0Pipeline::LastIndexedActiveCount() const
{
    return _lastIndexedActiveCount;
}

std::uint64_t D3D12CbtE0Pipeline::ClassificationSampleGeneration() const
{
    return _classificationSampleGeneration;
}

bool D3D12CbtE0Pipeline::IsInitialized() const
{
    return _initialized;
}
} // namespace ParallelRoam::Algorithms::Cbt2024::D3D12
