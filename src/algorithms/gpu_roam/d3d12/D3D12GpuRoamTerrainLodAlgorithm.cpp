#include "algorithms/gpu_roam/d3d12/D3D12GpuRoamTerrainLodAlgorithm.h"

#include "algorithms/TerrainLodProfiling.h"
#include "algorithms/gpu_roam/GpuRoamBufferSchema.h"
#include "render/D3D12GraphicsBackend.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace ParallelRoam::Algorithms::GpuRoam::D3D12
{
namespace
{
// 必须与 HLSL numthreads 的 X 维保持一致
constexpr std::uint32_t WorkGroupSize = 128U;
// 每帧描述符布局固定为九个 UAV 和一个高度图 SRV
constexpr std::uint32_t DescriptorsPerFrame = 10U;
constexpr std::uint32_t UavDescriptorCount = 9U;
// D3D12 根 CBV 地址要求 256 字节对齐
constexpr std::size_t ConstantBufferBytes = 256U;

/// <summary>
/// 与 HLSL Counters 缓冲布局一致的 GPU 计数器
/// </summary>
struct GpuCounters
{
    std::uint32_t ActiveLeafCount{0}; // compaction 生成的稠密叶数量
    std::uint32_t SplitCandidateCount{0}; // candidate pass 的 split 输出长度
    std::uint32_t MergeCandidateCount{0}; // candidate pass 的 merge 输出长度
    std::uint32_t Reserved{0}; // 保持 HLSL 结构布局稳定
    std::uint32_t SplitOnlyCommitCount{0}; // 本轮实际提交的 split 数量
    std::uint32_t AllocatedNodeCount{0}; // 节点池原子尾指针
};

/// <summary>
/// GPU ROAM-like 计算入口共享的常量缓冲布局
/// </summary>
struct GpuConstants
{
    // 节点池范围和活动叶输出上限
    std::uint32_t NodeCount{0}; // CPU 快照有效节点数
    std::uint32_t NodeCapacity{0}; // GPU 可访问的节点池上界
    std::uint32_t ActiveLeafLimit{0}; // compaction 和 emit 的叶容量
    std::uint32_t MaxDepth{0}; // split 与定义域解码深度上限
    // HLSL 通过两个 uint 保存 64 位构建序列号
    std::uint32_t BuildSequenceLow{0}; // 构建序列低 32 位
    std::uint32_t BuildSequenceHigh{0}; // 构建序列高 32 位
    std::uint32_t HeightMapWidth{0}; // 高度纹理采样宽度
    std::uint32_t HeightMapHeight{0}; // 高度纹理采样高度
    // 地形参数和 split merge 阈值与统一算法设置保持一致
    float TerrainSize{0.0F}; // 归一化定义域的水平缩放
    float HeightScale{0.0F}; // 高度纹理的垂直缩放
    float DistanceScale{0.0F}; // 屏幕误差距离权重
    float SplitThreshold{0.0F}; // split 候选阈值
    float MergeThreshold{0.0F}; // merge 候选阈值
    glm::vec3 CameraPosition{0.0F}; // 世界空间评分视点
};

// 锁定 CPU 回读布局和共享顶点布局，防止着色器侧索引错位
static_assert(sizeof(GpuCounters) == 24U);
static_assert(sizeof(Terrain::TerrainMeshVertex) == 13U * sizeof(float));

/// <summary>
/// 默认堆缓冲及其可选上传副本
/// </summary>
struct GpuBuffer
{
    Microsoft::WRL::ComPtr<ID3D12Resource> Resource; // GPU 读写的默认堆资源
    Microsoft::WRL::ComPtr<ID3D12Resource> Upload; // CPU 写入的上传堆副本
    std::uint8_t* MappedUpload{nullptr}; // 持久映射的上传地址
    // 默认堆和上传堆分别按需增长
    std::size_t CapacityBytes{0}; // 默认堆当前容量
    std::size_t UploadCapacityBytes{0}; // 上传堆当前容量
    std::uint32_t StructureStride{0}; // UAV 结构化元素步长
    D3D12_RESOURCE_STATES State{D3D12_RESOURCE_STATE_COMMON}; // 显式 barrier 的前态
};

/// <summary>
/// 单个交换链帧独占的计算输入、输出和回读状态
/// </summary>
struct GpuFrameResources
{
    // 拓扑计算的节点池、活动叶、误差和候选缓冲
    GpuBuffer Nodes; // CPU 快照和 GPU 新节点共享池
    GpuBuffer ActiveLeaves; // 两次 compaction 的稠密输出
    GpuBuffer ScreenErrors; // 每个活动叶的误差评分
    GpuBuffer Counters; // 原子计数和节点池尾指针
    GpuBuffer SplitCandidates; // split 候选节点索引
    GpuBuffer MergeCandidates; // merge 候选父节点索引
    // emit pass 输出可直接绘制的网格和间接参数
    GpuBuffer Vertices; // 图形管线直接消费的顶点流
    GpuBuffer Indices; // 图形管线直接消费的索引流
    GpuBuffer IndirectArgs; // ExecuteIndirect 的命令参数
    Microsoft::WRL::ComPtr<ID3D12Resource> ConstantBuffer; // 当前帧共享 pass 常量
    std::uint8_t* MappedConstants{nullptr}; // 256 字节对齐的持久映射地址
    bool PendingReadback{false}; // 该帧槽位是否有待消费结果
};

D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CreationNodeMask = 1;
    properties.VisibleNodeMask = 1;
    return properties;
}

D3D12_RESOURCE_DESC BufferDescription(std::size_t bytes, D3D12_RESOURCE_FLAGS flags)
{
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    // 至少保留一个标量元素，避免 D3D12 零大小资源
    description.Width = std::max<std::size_t>(bytes, 4U);
    description.Height = 1;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    description.Flags = flags;
    return description;
}

void SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
}

std::vector<std::uint8_t> ReadShader(const char* name, std::string* errorMessage)
{
#if defined(PARALLEL_ROAM_DX12_SHADER_DIR)
    const std::filesystem::path directory{PARALLEL_ROAM_DX12_SHADER_DIR};
#else
    const std::filesystem::path directory{"assets/shaders/dx12"};
#endif
    const std::filesystem::path path = directory / name;
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream)
    {
        SetError(errorMessage, "Failed to open DX12 GPU ROAM-like shader: " + path.string());
        return {};
    }
    const std::streamsize size = stream.tellg();
    if (size <= 0)
    {
        SetError(errorMessage, "DX12 GPU ROAM-like shader is empty: " + path.string());
        return {};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        SetError(errorMessage, "Failed to read DX12 GPU ROAM-like shader: " + path.string());
        return {};
    }
    return bytes;
}

std::uint32_t DispatchCount(std::size_t itemCount)
{
    // 即使输入为空也提交一个组，由着色器边界检查保持输出可预测
    return static_cast<std::uint32_t>((std::max<std::size_t>(itemCount, 1U) + WorkGroupSize - 1U) / WorkGroupSize);
}

std::uint32_t Low32(std::uint64_t value)
{
    return static_cast<std::uint32_t>(value & 0xffffffffULL);
}

std::uint32_t High32(std::uint64_t value)
{
    return static_cast<std::uint32_t>(value >> 32U);
}

DataOrientedRoam::DataOrientedRoamSettings ToDataSettings(const TerrainLodSettings& settings)
{
    DataOrientedRoam::DataOrientedRoamSettings result{};
    result.MaxDepth = settings.MaxDepth;
    result.SplitThreshold = settings.SplitThreshold;
    result.MergeThreshold = settings.MergeThreshold;
    result.DistanceScale = settings.DistanceScale;
    result.ErrorEvaluationWorkerCount = 0U;
    result.EnableLocalConstraints = settings.EnableLocalConstraints;
    result.EnableTopologyValidation = settings.EnableTopologyValidation;
    return result;
}

TerrainLodStats ToLodStats(const DataOrientedRoam::DataOrientedRoamStats& stats)
{
    TerrainLodStats result{};
    result.ActiveTriangleCount = stats.ActiveTriangleCount;
    result.ActiveNodeCount = stats.NodeCount;
    result.OriginalTriangleCount = stats.OriginalTriangleCount;
    result.SubdividedTriangleCount = stats.SubdividedTriangleCount;
    result.RebuiltTriangleCount = stats.RebuiltTriangleCount;
    result.ActiveSplitCount = stats.ActiveSplitCount;
    result.SplitCount = stats.SplitCount;
    result.ForcedSplitCount = stats.ForcedSplitCount;
    result.MergeCount = stats.MergeCount;
    result.CrackRiskCount = stats.CrackRiskCount;
    result.ConstraintPassCount = stats.ConstraintPassCount;
    result.CandidatePeakCount = stats.CandidatePeakCount;
    result.RejectedSplitCount = stats.RejectedSplitCount;
    result.RejectedMergeCount = stats.RejectedMergeCount;
    result.TjunctionCount = stats.TjunctionCount;
    result.InvalidNeighborCount = stats.InvalidNeighborCount;
    result.InvalidTopologyCount = stats.InvalidTopologyCount;
    result.CpuWorkerCount = std::max<std::size_t>(stats.ErrorEvaluationWorkerCount, 1U);
    result.CpuUpdateMilliseconds = stats.UpdateMilliseconds;
    result.CpuErrorEvalMilliseconds = stats.ErrorEvaluationWorkerCount > 1U
        ? stats.ErrorEvaluationParallelMilliseconds
        : stats.ErrorEvaluationSingleThreadMilliseconds;
    result.CpuCollectMilliseconds = stats.ActiveLeafCollectMilliseconds +
        stats.SplitCandidateMarkMilliseconds + stats.MergeCandidateMarkMilliseconds;
    result.CpuMeshBuildMilliseconds = stats.EmitMilliseconds;
    result.SplitMilliseconds = stats.SplitMilliseconds;
    result.MergeMilliseconds = stats.MergeMilliseconds;
    result.EmitMilliseconds = stats.EmitMilliseconds;
    result.ValidateMilliseconds = stats.ValidateMilliseconds;
    result.MaxActiveDepth = stats.MaxDepthReached;
    return result;
}

bool CreateMappedUpload(
    ID3D12Device* device,
    std::size_t bytes,
    Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
    std::uint8_t*& mapped,
    std::string* errorMessage)
{
    // 上传资源生命周期内保持映射，减少每帧 Map 调用
    const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC description = BufferDescription(bytes, D3D12_RESOURCE_FLAG_NONE);
    HRESULT result = device->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &description,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&resource));
    if (FAILED(result))
    {
        SetError(errorMessage, "Failed to allocate DX12 GPU ROAM-like upload buffer");
        return false;
    }
    const D3D12_RANGE noRead{0, 0};
    void* memory = nullptr;
    result = resource->Map(0, &noRead, &memory);
    if (FAILED(result))
    {
        SetError(errorMessage, "Failed to map DX12 GPU ROAM-like upload buffer");
        resource.Reset();
        return false;
    }
    mapped = static_cast<std::uint8_t*>(memory);
    return true;
}

void ReleaseBuffer(GpuBuffer& buffer)
{
    // 只有上传副本允许映射，默认堆始终只由 GPU 访问
    if (buffer.Upload != nullptr && buffer.MappedUpload != nullptr)
    {
        buffer.Upload->Unmap(0, nullptr);
    }
    buffer = {};
}

void Transition(
    ID3D12GraphicsCommandList* commandList,
    GpuBuffer& buffer,
    D3D12_RESOURCE_STATES nextState)
{
    // 由包装对象追踪状态，避免重复 barrier 和状态前值不一致
    if (buffer.Resource == nullptr || buffer.State == nextState)
    {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = buffer.Resource.Get();
    barrier.Transition.StateBefore = buffer.State;
    barrier.Transition.StateAfter = nextState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
    buffer.State = nextState;
}

void UavBarrier(ID3D12GraphicsCommandList* commandList)
{
    // 全局 UAV barrier 保证前一计算入口的写入对下一入口可见
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = nullptr;
    commandList->ResourceBarrier(1, &barrier);
}
} // namespace

/// <summary>
/// GPU ROAM-like 算法跨帧持有的 D3D12 状态
/// </summary>
struct D3D12GpuRoamState
{
    // Backend 由 Application 持有，算法状态只借用
    Render::D3D12GraphicsBackend* Backend{nullptr};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> RootSignature; // 六个 pass 的公共根签名
    // 六个 compute pass 共享同一根签名和描述符布局
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CompactPipeline; // 活动叶压缩 PSO
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ErrorPipeline; // 误差计算 PSO
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CandidatePipeline; // 候选分类 PSO
    Microsoft::WRL::ComPtr<ID3D12PipelineState> SplitPipeline; // split-only 提交 PSO
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ResetLeavesPipeline; // 活动叶计数清零 PSO
    Microsoft::WRL::ComPtr<ID3D12PipelineState> EmitPipeline; // 网格和命令生成 PSO
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DescriptorHeap; // 按帧分块的 shader-visible 堆
    std::uint32_t DescriptorSize{0}; // 当前设备描述符步长
    std::array<GpuFrameResources, Render::D3D12GraphicsBackend::FrameCount> Frames; // fence 隔离的帧资源
    // 高度图在路径和尺寸不变时跨帧复用
    Microsoft::WRL::ComPtr<ID3D12Resource> HeightMapTexture; // R32_FLOAT 默认堆纹理
    std::filesystem::path HeightMapPath; // 高度图缓存路径键
    int HeightMapWidth{0}; // 缓存纹理宽度
    int HeightMapHeight{0}; // 缓存纹理高度
    // 时间戳和计数器按 frameIndex 延迟回读
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> QueryHeap; // 每帧两个时间戳槽位
    Microsoft::WRL::ComPtr<ID3D12Resource> QueryReadback; // 所有帧共享的时间戳回读堆
    Microsoft::WRL::ComPtr<ID3D12Resource> CounterReadback; // 所有帧共享的计数器回读堆
    std::uint64_t TimestampFrequency{0}; // 命令队列时间戳频率
    GpuCounters LastCounters{}; // 最近完成帧的可靠计数
    float LastGpuMilliseconds{0.0F}; // 最近完成帧的 compute 时间
    std::uint64_t Generation{0}; // 原生资源包版本号

    ~D3D12GpuRoamState()
    {
        // 外层销毁前必须确保后端队列空闲，析构只负责解除持久映射
        for (GpuFrameResources& frame : Frames)
        {
            ReleaseBuffer(frame.Nodes);
            ReleaseBuffer(frame.ActiveLeaves);
            ReleaseBuffer(frame.ScreenErrors);
            ReleaseBuffer(frame.Counters);
            ReleaseBuffer(frame.SplitCandidates);
            ReleaseBuffer(frame.MergeCandidates);
            ReleaseBuffer(frame.Vertices);
            ReleaseBuffer(frame.Indices);
            ReleaseBuffer(frame.IndirectArgs);
            if (frame.ConstantBuffer != nullptr && frame.MappedConstants != nullptr)
            {
                frame.ConstantBuffer->Unmap(0, nullptr);
            }
        }
    }
};

namespace
{
bool CreateComputePipeline(
    D3D12GpuRoamState& state,
    const char* shaderName,
    Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipeline,
    std::string* errorMessage)
{
    // 每个入口单独编译为字节码，共享同一资源绑定布局
    const std::vector<std::uint8_t> shader = ReadShader(shaderName, errorMessage);
    if (shader.empty())
    {
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
    description.pRootSignature = state.RootSignature.Get();
    description.CS = {shader.data(), shader.size()};
    if (FAILED(state.Backend->Device()->CreateComputePipelineState(&description, IID_PPV_ARGS(&pipeline))))
    {
        SetError(errorMessage, std::string{"Failed to create DX12 GPU ROAM-like pipeline: "} + shaderName);
        return false;
    }
    return true;
}

bool InitializeState(D3D12GpuRoamState& state, std::string* errorMessage)
{
    // 根参数顺序固定为 b0 常量 九个 UAV 和一个高度图 SRV
    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = UavDescriptorCount;
    uavRange.BaseShaderRegister = 0;
    uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    std::array<D3D12_ROOT_PARAMETER, 3> parameters{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &uavRange;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    parameters[2].DescriptorTable.pDescriptorRanges = &srvRange;

    // 高度图边界采用 clamp，有限差分法向不会跨越纹理边缘
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;

    D3D12_ROOT_SIGNATURE_DESC rootDescription{};
    rootDescription.NumParameters = static_cast<UINT>(parameters.size());
    rootDescription.pParameters = parameters.data();
    rootDescription.NumStaticSamplers = 1;
    rootDescription.pStaticSamplers = &sampler;
    Microsoft::WRL::ComPtr<ID3DBlob> serialized;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    HRESULT result = D3D12SerializeRootSignature(
        &rootDescription,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serialized,
        &errors);
    if (FAILED(result))
    {
        const char* detail = errors != nullptr ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown";
        SetError(errorMessage, std::string{"Failed to serialize DX12 GPU ROAM-like root signature: "} + detail);
        return false;
    }
    if (FAILED(state.Backend->Device()->CreateRootSignature(
            0,
            serialized->GetBufferPointer(),
            serialized->GetBufferSize(),
            IID_PPV_ARGS(&state.RootSignature))))
    {
        SetError(errorMessage, "Failed to create DX12 GPU ROAM-like root signature");
        return false;
    }

    if (!CreateComputePipeline(state, "GpuRoamCompact.cso", state.CompactPipeline, errorMessage) ||
        !CreateComputePipeline(state, "GpuRoamError.cso", state.ErrorPipeline, errorMessage) ||
        !CreateComputePipeline(state, "GpuRoamCandidates.cso", state.CandidatePipeline, errorMessage) ||
        !CreateComputePipeline(state, "GpuRoamSplit.cso", state.SplitPipeline, errorMessage) ||
        !CreateComputePipeline(state, "GpuRoamResetLeaves.cso", state.ResetLeavesPipeline, errorMessage) ||
        !CreateComputePipeline(state, "GpuRoamEmit.cso", state.EmitPipeline, errorMessage))
    {
        return false;
    }

    // 描述符按帧连续分块，计算时只需偏移到当前帧起点
    D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
    heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDescription.NumDescriptors = DescriptorsPerFrame * Render::D3D12GraphicsBackend::FrameCount;
    heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(state.Backend->Device()->CreateDescriptorHeap(
            &heapDescription,
            IID_PPV_ARGS(&state.DescriptorHeap))))
    {
        SetError(errorMessage, "Failed to create DX12 GPU ROAM-like descriptor heap");
        return false;
    }
    state.DescriptorSize = state.Backend->Device()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // 常量缓冲按帧拆分，后端 fence 保证复用前 GPU 已完成读取
    for (GpuFrameResources& frame : state.Frames)
    {
        if (!CreateMappedUpload(
                state.Backend->Device(),
                ConstantBufferBytes,
                frame.ConstantBuffer,
                frame.MappedConstants,
                errorMessage))
        {
            return false;
        }
    }

    // 查询和回读区域都按帧索引固定布局，结果延迟到该槽位再次复用时读取
    D3D12_QUERY_HEAP_DESC queryDescription{};
    queryDescription.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryDescription.Count = Render::D3D12GraphicsBackend::FrameCount * 2U;
    if (FAILED(state.Backend->Device()->CreateQueryHeap(&queryDescription, IID_PPV_ARGS(&state.QueryHeap))))
    {
        SetError(errorMessage, "Failed to create DX12 GPU ROAM-like timestamp heap");
        return false;
    }
    const D3D12_HEAP_PROPERTIES readbackHeap = HeapProperties(D3D12_HEAP_TYPE_READBACK);
    D3D12_RESOURCE_DESC queryReadbackDescription = BufferDescription(
        Render::D3D12GraphicsBackend::FrameCount * 2U * sizeof(std::uint64_t),
        D3D12_RESOURCE_FLAG_NONE);
    if (FAILED(state.Backend->Device()->CreateCommittedResource(
            &readbackHeap,
            D3D12_HEAP_FLAG_NONE,
            &queryReadbackDescription,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&state.QueryReadback))))
    {
        SetError(errorMessage, "Failed to create DX12 GPU ROAM-like timestamp readback");
        return false;
    }
    D3D12_RESOURCE_DESC counterReadbackDescription = BufferDescription(
        Render::D3D12GraphicsBackend::FrameCount * sizeof(GpuCounters),
        D3D12_RESOURCE_FLAG_NONE);
    if (FAILED(state.Backend->Device()->CreateCommittedResource(
            &readbackHeap,
            D3D12_HEAP_FLAG_NONE,
            &counterReadbackDescription,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&state.CounterReadback))))
    {
        SetError(errorMessage, "Failed to create DX12 GPU ROAM-like counter readback");
        return false;
    }
    if (FAILED(state.Backend->CommandQueue()->GetTimestampFrequency(&state.TimestampFrequency)))
    {
        SetError(errorMessage, "Failed to query DX12 GPU timestamp frequency");
        return false;
    }
    return true;
}

bool EnsureGpuBuffer(
    D3D12GpuRoamState& state,
    GpuBuffer& buffer,
    std::size_t requiredBytes,
    std::uint32_t structureStride,
    bool needsUpload,
    std::uint32_t descriptorIndex,
    std::string* errorMessage)
{
    requiredBytes = std::max<std::size_t>(requiredBytes, structureStride);
    if (buffer.Resource == nullptr || buffer.CapacityBytes < requiredBytes)
    {
        // 只在容量不足时增长，避免每次 LOD 波动都重新分配
        ReleaseBuffer(buffer);
        const D3D12_HEAP_PROPERTIES defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
        const D3D12_RESOURCE_DESC description = BufferDescription(
            requiredBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        // CPU 快照缓冲先进入复制目标，其余纯 GPU 输出直接进入 UAV
        const D3D12_RESOURCE_STATES initialState = needsUpload
            ? D3D12_RESOURCE_STATE_COPY_DEST
            : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        if (FAILED(state.Backend->Device()->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &description,
                initialState,
                nullptr,
                IID_PPV_ARGS(&buffer.Resource))))
        {
            SetError(errorMessage, "Failed to allocate DX12 GPU ROAM-like default buffer");
            return false;
        }
        buffer.CapacityBytes = requiredBytes;
        buffer.StructureStride = structureStride;
        buffer.State = initialState;
    }
    if (needsUpload && (buffer.Upload == nullptr || buffer.UploadCapacityBytes < requiredBytes))
    {
        // 上传副本与默认堆分别扩容，二者容量契约不隐式绑定
        if (buffer.Upload != nullptr && buffer.MappedUpload != nullptr)
        {
            buffer.Upload->Unmap(0, nullptr);
        }
        buffer.Upload.Reset();
        buffer.MappedUpload = nullptr;
        if (!CreateMappedUpload(
                state.Backend->Device(),
                requiredBytes,
                buffer.Upload,
                buffer.MappedUpload,
                errorMessage))
        {
            return false;
        }
        buffer.UploadCapacityBytes = requiredBytes;
    }

    // 资源扩容后重写固定描述符槽，GPU 句柄偏移保持不变
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDescription{};
    uavDescription.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDescription.Format = DXGI_FORMAT_UNKNOWN;
    uavDescription.Buffer.NumElements = static_cast<UINT>(buffer.CapacityBytes / structureStride);
    uavDescription.Buffer.StructureByteStride = structureStride;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = state.DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(descriptorIndex) * state.DescriptorSize;
    state.Backend->Device()->CreateUnorderedAccessView(buffer.Resource.Get(), nullptr, &uavDescription, cpu);
    return true;
}

bool EnsureFrameResources(
    D3D12GpuRoamState& state,
    std::uint32_t frameIndex,
    std::size_t nodeCapacity,
    std::string* errorMessage)
{
    GpuFrameResources& frame = state.Frames[frameIndex];
    // 当前帧的九个 UAV 从固定分块起点连续排列
    const std::uint32_t base = frameIndex * DescriptorsPerFrame;
    const std::size_t nodeBytes = nodeCapacity * sizeof(GpuRoamNodeRecord);
    const std::size_t scalarBytes = nodeCapacity * sizeof(std::uint32_t);
    const std::size_t vertexBytes = nodeCapacity * 3U * sizeof(Terrain::TerrainMeshVertex);
    const std::size_t indexBytes = nodeCapacity * 3U * sizeof(std::uint32_t);
    return EnsureGpuBuffer(state, frame.Nodes, nodeBytes, sizeof(GpuRoamNodeRecord), true, base + 0U, errorMessage) &&
           EnsureGpuBuffer(state, frame.ActiveLeaves, scalarBytes, sizeof(std::uint32_t), false, base + 1U, errorMessage) &&
           EnsureGpuBuffer(state, frame.ScreenErrors, scalarBytes, sizeof(float), false, base + 2U, errorMessage) &&
           EnsureGpuBuffer(state, frame.Counters, sizeof(GpuCounters), sizeof(std::uint32_t), true, base + 3U, errorMessage) &&
           EnsureGpuBuffer(state, frame.SplitCandidates, scalarBytes, sizeof(std::uint32_t), false, base + 4U, errorMessage) &&
           EnsureGpuBuffer(state, frame.MergeCandidates, scalarBytes, sizeof(std::uint32_t), false, base + 5U, errorMessage) &&
           EnsureGpuBuffer(state, frame.Vertices, vertexBytes, sizeof(float), false, base + 6U, errorMessage) &&
           EnsureGpuBuffer(state, frame.Indices, indexBytes, sizeof(std::uint32_t), false, base + 7U, errorMessage) &&
           EnsureGpuBuffer(state, frame.IndirectArgs, sizeof(D3D12_DRAW_INDEXED_ARGUMENTS), sizeof(std::uint32_t), false, base + 8U, errorMessage);
}

bool EnsureHeightMap(
    D3D12GpuRoamState& state,
    const Terrain::HeightMap& heightMap,
    std::string* errorMessage)
{
    // 路径和尺寸不变时复用 GPU 纹理，避免相机更新重复上传
    if (state.HeightMapTexture != nullptr &&
        state.HeightMapPath == heightMap.SourcePath() &&
        state.HeightMapWidth == heightMap.Width() &&
        state.HeightMapHeight == heightMap.Height())
    {
        return true;
    }

    // 所有帧描述符都将改写，先确保不存在引用旧纹理的在途命令
    state.Backend->WaitForGpuIdle();
    D3D12_RESOURCE_DESC textureDescription{};
    textureDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDescription.Width = static_cast<UINT64>(heightMap.Width());
    textureDescription.Height = static_cast<UINT>(heightMap.Height());
    textureDescription.DepthOrArraySize = 1;
    textureDescription.MipLevels = 1;
    textureDescription.Format = DXGI_FORMAT_R32_FLOAT;
    textureDescription.SampleDesc.Count = 1;
    textureDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    const D3D12_HEAP_PROPERTIES defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    if (FAILED(state.Backend->Device()->CreateCommittedResource(
            &defaultHeap,
            D3D12_HEAP_FLAG_NONE,
            &textureDescription,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&texture))))
    {
        SetError(errorMessage, "Failed to allocate DX12 GPU ROAM-like height map");
        return false;
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rowCount = 0;
    UINT64 rowSize = 0;
    UINT64 uploadBytes = 0;
    // 设备决定上传行跨度，不能按紧密浮点行直接复制
    state.Backend->Device()->GetCopyableFootprints(
        &textureDescription, 0, 1, 0, &footprint, &rowCount, &rowSize, &uploadBytes);
    Microsoft::WRL::ComPtr<ID3D12Resource> upload;
    std::uint8_t* mapped = nullptr;
    if (!CreateMappedUpload(
            state.Backend->Device(),
            static_cast<std::size_t>(uploadBytes),
            upload,
            mapped,
            errorMessage))
    {
        return false;
    }
    for (UINT y = 0; y < rowCount; ++y)
    {
        float* row = reinterpret_cast<float*>(
            mapped + footprint.Offset + static_cast<std::size_t>(y) * footprint.Footprint.RowPitch);
        for (int x = 0; x < heightMap.Width(); ++x)
        {
            row[x] = heightMap.SamplePixel(x, static_cast<int>(y));
        }
    }

    // 立即提交完成后局部上传缓冲即可解除映射和释放
    const bool uploaded = state.Backend->ExecuteImmediate(
        [&](ID3D12GraphicsCommandList* commandList, std::string*) {
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = upload.Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint = footprint;
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = texture.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = texture.Get();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList->ResourceBarrier(1, &barrier);
            return true;
        },
        errorMessage);
    upload->Unmap(0, nullptr);
    if (!uploaded)
    {
        return false;
    }
    state.HeightMapTexture = texture;
    state.HeightMapPath = heightMap.SourcePath();
    state.HeightMapWidth = heightMap.Width();
    state.HeightMapHeight = heightMap.Height();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDescription{};
    srvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDescription.Format = DXGI_FORMAT_R32_FLOAT;
    srvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDescription.Texture2D.MipLevels = 1;
    for (std::uint32_t frameIndex = 0; frameIndex < Render::D3D12GraphicsBackend::FrameCount; ++frameIndex)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = state.DescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += static_cast<SIZE_T>(frameIndex * DescriptorsPerFrame + 9U) * state.DescriptorSize;
        state.Backend->Device()->CreateShaderResourceView(state.HeightMapTexture.Get(), &srvDescription, cpu);
    }
    return true;
}

void ReadCompletedResults(D3D12GpuRoamState& state, std::uint32_t frameIndex, TerrainLodStats& stats)
{
    GpuFrameResources& frame = state.Frames[frameIndex];
    // 后端已经等待当前帧槽位的 fence，回读不会阻塞 CPU
    if (!frame.PendingReadback)
    {
        return;
    }

    const std::size_t counterOffset = frameIndex * sizeof(GpuCounters);
    D3D12_RANGE counterRange{counterOffset, counterOffset + sizeof(GpuCounters)};
    void* mappedCounters = nullptr;
    if (SUCCEEDED(state.CounterReadback->Map(0, &counterRange, &mappedCounters)))
    {
        const auto* counters = reinterpret_cast<const GpuCounters*>(
            static_cast<const std::uint8_t*>(mappedCounters) + counterOffset);
        state.LastCounters = *counters;
        state.CounterReadback->Unmap(0, nullptr);
    }

    // 计算耗时由该帧第一个和最后一个计算调度外侧时间戳得出
    const std::size_t queryOffset = frameIndex * 2U * sizeof(std::uint64_t);
    D3D12_RANGE queryRange{queryOffset, queryOffset + 2U * sizeof(std::uint64_t)};
    void* mappedQueries = nullptr;
    if (SUCCEEDED(state.QueryReadback->Map(0, &queryRange, &mappedQueries)))
    {
        const auto* timestamps = reinterpret_cast<const std::uint64_t*>(
            static_cast<const std::uint8_t*>(mappedQueries) + queryOffset);
        if (timestamps[1] >= timestamps[0] && state.TimestampFrequency > 0U)
        {
            state.LastGpuMilliseconds = static_cast<float>(
                static_cast<double>(timestamps[1] - timestamps[0]) * 1000.0 /
                static_cast<double>(state.TimestampFrequency));
        }
        state.QueryReadback->Unmap(0, nullptr);
    }
    frame.PendingReadback = false;
    stats.GpuComputeMilliseconds = state.LastGpuMilliseconds;
    stats.CpuGpuReadbackBytes = sizeof(GpuCounters) + 2U * sizeof(std::uint64_t);
}

void UploadSnapshot(
    ID3D12GraphicsCommandList* commandList,
    GpuFrameResources& frame,
    const GpuRoamBufferSnapshot& snapshot)
{
    // CPU 仅上传节点快照，活动叶列表由 GPU 重新压缩得到
    const std::size_t nodeBytes = snapshot.NodeBufferBytes();
    std::memcpy(frame.Nodes.MappedUpload, snapshot.Nodes.data(), nodeBytes);
    Transition(commandList, frame.Nodes, D3D12_RESOURCE_STATE_COPY_DEST);
    commandList->CopyBufferRegion(frame.Nodes.Resource.Get(), 0, frame.Nodes.Upload.Get(), 0, nodeBytes);
    Transition(commandList, frame.Nodes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // 清零阶段计数器并从 CPU 快照节点数初始化节点池尾指针
    GpuCounters counters{};
    counters.AllocatedNodeCount = static_cast<std::uint32_t>(snapshot.Nodes.size());
    std::memcpy(frame.Counters.MappedUpload, &counters, sizeof(counters));
    Transition(commandList, frame.Counters, D3D12_RESOURCE_STATE_COPY_DEST);
    commandList->CopyBufferRegion(
        frame.Counters.Resource.Get(), 0, frame.Counters.Upload.Get(), 0, sizeof(counters));
    Transition(commandList, frame.Counters, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void DispatchPipeline(
    ID3D12GraphicsCommandList* commandList,
    ID3D12PipelineState* pipeline,
    std::uint32_t groupCount)
{
    commandList->SetPipelineState(pipeline);
    commandList->Dispatch(std::max(groupCount, 1U), 1, 1);
    // 所有入口共享 UAV，阶段间必须建立写后读顺序
    UavBarrier(commandList);
}
} // namespace

D3D12GpuRoamTerrainLodAlgorithm::D3D12GpuRoamTerrainLodAlgorithm(
    Render::D3D12GraphicsBackend& backend)
    : _backend{&backend}, _state{std::make_unique<D3D12GpuRoamState>()}
{
    _state->Backend = &backend;
}

D3D12GpuRoamTerrainLodAlgorithm::~D3D12GpuRoamTerrainLodAlgorithm() = default;

TerrainLodAlgorithmInfo D3D12GpuRoamTerrainLodAlgorithm::Info() const
{
    return {
        TerrainLodAlgorithmId::GpuRoamLike,
        "d3d12_gpu_roam_like",
        "GPU ROAM-like (DX12)",
        "DX12 compute topology refinement, mesh emission and indirect drawing",
    };
}

TerrainLodAlgorithmCapabilities D3D12GpuRoamTerrainLodAlgorithm::Capabilities() const
{
    // merge 候选目前只统计不提交，能力标记沿用桥接基线的 CPU 拓扑能力
    TerrainLodAlgorithmCapabilities capabilities{};
    capabilities.SupportsGpuDrivenRendering = true;
    capabilities.SupportsSplit = true;
    capabilities.SupportsMerge = true;
    capabilities.SupportsCrackFix = true;
    capabilities.SupportsTopologyValidation = true;
    return capabilities;
}

bool D3D12GpuRoamTerrainLodAlgorithm::BuildRenderData(
    const TerrainLodBuildInput& input,
    TerrainLodRenderPacket& outPacket,
    std::string* errorMessage)
{
    // 计算命令直接写入后端当前命令列表，调用必须位于打开的帧内
    outPacket = {};
    _stats = {};
    if (_backend == nullptr || !_backend->FrameOpen() || input.HeightMap == nullptr || !input.HeightMap->IsValid())
    {
        SetError(errorMessage, "DX12 GPU ROAM-like requires an open frame and a valid height map");
        return false;
    }
    if (_state->RootSignature == nullptr && !InitializeState(*_state, errorMessage))
    {
        return false;
    }
    if (!EnsureHeightMap(*_state, *input.HeightMap, errorMessage))
    {
        return false;
    }

    // 当前桥接实现仍先在 CPU 完成 DOD ROAM 拓扑更新
    const TerrainLodCpuSample cpuStart = CaptureTerrainLodCpuSample();
    _cpuTopologyBuilder.UpdateTopology(
        *input.HeightMap,
        input.Settings.TerrainSize,
        input.Settings.HeightScale,
        input.View.CameraPosition,
        ToDataSettings(input.Settings));
    _stats = ToLodStats(_cpuTopologyBuilder.Stats());
    // 将 SoA CPU 状态打包为与 HLSL NodeRecord 一致的结构化快照
    const auto snapshotStart = std::chrono::steady_clock::now();
    const GpuRoamBufferSnapshot snapshot = BuildGpuRoamBufferSnapshot(_cpuTopologyBuilder.State());
    _stats.GpuSnapshotBuildMilliseconds =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - snapshotStart).count();
    if (snapshot.Nodes.empty() || snapshot.ActiveLeafIndices.empty())
    {
        SetError(errorMessage, "DX12 GPU ROAM-like topology snapshot is empty");
        return false;
    }

    // 当前帧 fence 已完成，因此可以先读取上次使用该槽位的延迟结果
    const std::uint32_t frameIndex = _backend->CurrentFrameIndex();
    ReadCompletedResults(*_state, frameIndex, _stats);
    // 为每个输入活动叶节点预留一对潜在子节点，空间不足时着色器回滚分裂
    const std::size_t nodeCapacity = snapshot.Nodes.size() + snapshot.ActiveLeafIndices.size() * 2U;
    const auto allocationStart = std::chrono::steady_clock::now();
    if (!EnsureFrameResources(*_state, frameIndex, nodeCapacity, errorMessage))
    {
        return false;
    }
    _stats.GpuBufferAllocationMilliseconds =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - allocationStart).count();
    GpuFrameResources& frame = _state->Frames[frameIndex];
    ID3D12GraphicsCommandList* commandList = _backend->CommandList();
    const auto dispatchStart = std::chrono::steady_clock::now();

    // 上一轮这些缓冲可能处于图形读取状态，计算前统一转回 UAV
    Transition(commandList, frame.Vertices, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, frame.Indices, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(commandList, frame.IndirectArgs, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    UploadSnapshot(commandList, frame, snapshot);

    // 常量布局必须与 HLSL GpuRoamConstants 的字段顺序一致
    GpuConstants constants{};
    constants.NodeCount = static_cast<std::uint32_t>(snapshot.Nodes.size());
    constants.NodeCapacity = static_cast<std::uint32_t>(nodeCapacity);
    constants.ActiveLeafLimit = static_cast<std::uint32_t>(nodeCapacity);
    constants.MaxDepth = static_cast<std::uint32_t>(std::max(snapshot.MaxDepth, 0));
    constants.BuildSequenceLow = Low32(snapshot.BuildSequence);
    constants.BuildSequenceHigh = High32(snapshot.BuildSequence);
    constants.HeightMapWidth = static_cast<std::uint32_t>(input.HeightMap->Width());
    constants.HeightMapHeight = static_cast<std::uint32_t>(input.HeightMap->Height());
    constants.TerrainSize = input.Settings.TerrainSize;
    constants.HeightScale = input.Settings.HeightScale;
    constants.DistanceScale = input.Settings.DistanceScale;
    constants.SplitThreshold = input.Settings.SplitThreshold;
    // merge 阈值不允许高于 split 阈值，避免同一节点同时进入冲突候选集
    constants.MergeThreshold = std::min(input.Settings.MergeThreshold, input.Settings.SplitThreshold);
    constants.CameraPosition = input.View.CameraPosition;
    std::memset(frame.MappedConstants, 0, ConstantBufferBytes);
    std::memcpy(frame.MappedConstants, &constants, sizeof(constants));

    // 当前帧九个 UAV 连续绑定，高度图 SRV 位于同一分块最后一个槽位
    ID3D12DescriptorHeap* heaps[] = {_state->DescriptorHeap.Get()};
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetComputeRootSignature(_state->RootSignature.Get());
    commandList->SetComputeRootConstantBufferView(0, frame.ConstantBuffer->GetGPUVirtualAddress());
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = _state->DescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += static_cast<UINT64>(frameIndex * DescriptorsPerFrame) * _state->DescriptorSize;
    commandList->SetComputeRootDescriptorTable(1, gpu);
    D3D12_GPU_DESCRIPTOR_HANDLE heightGpu = gpu;
    heightGpu.ptr += static_cast<UINT64>(9U) * _state->DescriptorSize;
    commandList->SetComputeRootDescriptorTable(2, heightGpu);

    // 时间戳包围完整计算链，不包含后续状态转换和回读复制
    const std::uint32_t queryStart = frameIndex * 2U;
    commandList->EndQuery(_state->QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryStart);
    // 从 CPU 快照重新压缩活动叶节点
    DispatchPipeline(commandList, _state->CompactPipeline.Get(), DispatchCount(nodeCapacity));
    // 对压缩后的叶节点计算距离加权误差
    DispatchPipeline(commandList, _state->ErrorPipeline.Get(), DispatchCount(nodeCapacity));
    // 同时生成 split 候选和仅用于统计的 merge 候选
    DispatchPipeline(
        commandList,
        _state->CandidatePipeline.Get(),
        DispatchCount(std::max(snapshot.Nodes.size(), nodeCapacity)));
    // 提交一次 base-neighbor 成对兼容分裂，暂不递归传播 compatibility chain
    DispatchPipeline(commandList, _state->SplitPipeline.Get(), DispatchCount(nodeCapacity));
    // 分裂改变活动叶集合，清零计数并重新压缩
    DispatchPipeline(commandList, _state->ResetLeavesPipeline.Get(), 1U);
    DispatchPipeline(commandList, _state->CompactPipeline.Get(), DispatchCount(nodeCapacity));
    // 展开最终叶节点网格并写入 DrawIndexed 间接参数
    DispatchPipeline(commandList, _state->EmitPipeline.Get(), DispatchCount(nodeCapacity));
    commandList->EndQuery(_state->QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, queryStart + 1U);

    // 计算输出随后在同一命令列表中由图形管线直接消费
    Transition(commandList, frame.Vertices, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    Transition(commandList, frame.Indices, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    Transition(commandList, frame.IndirectArgs, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    // 计数器和时间戳只异步复制，本次调用不等待 GPU 完成
    Transition(commandList, frame.Counters, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commandList->CopyBufferRegion(
        _state->CounterReadback.Get(),
        frameIndex * sizeof(GpuCounters),
        frame.Counters.Resource.Get(),
        0,
        sizeof(GpuCounters));
    Transition(commandList, frame.Counters, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandList->ResolveQueryData(
        _state->QueryHeap.Get(),
        D3D12_QUERY_TYPE_TIMESTAMP,
        queryStart,
        2,
        _state->QueryReadback.Get(),
        static_cast<std::uint64_t>(queryStart) * sizeof(std::uint64_t));
    // 结果在该帧槽位下一次通过 fence 验证后读取
    frame.PendingReadback = true;

    _stats.GpuDispatchWallMilliseconds =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - dispatchStart).count();
    _stats.GpuComputeMilliseconds = _state->LastGpuMilliseconds;
    _stats.CpuGpuUploadBytes = snapshot.NodeBufferBytes() + sizeof(GpuCounters);
    // 当前帧尚未执行完成，绘制统计先采用上次结果或 CPU 快照估值
    const std::size_t activeEstimate = _state->LastCounters.ActiveLeafCount > 0U
        ? _state->LastCounters.ActiveLeafCount
        : snapshot.ActiveLeafIndices.size();
    _stats.ActiveTriangleCount = activeEstimate;
    _stats.ActiveNodeCount = std::max<std::size_t>(_stats.ActiveNodeCount, _state->LastCounters.AllocatedNodeCount);
    _stats.SubdividedTriangleCount += _state->LastCounters.SplitOnlyCommitCount;
    _stats.MaxActiveDepth = std::max(_stats.MaxActiveDepth, snapshot.MaxDepthReached);

    // 数据包返回算法拥有的原生资源，渲染器不得释放或跨重建缓存
    outPacket.Mode = TerrainLodRenderMode::GpuIndirect;
    outPacket.StatusMessage = "DX12 compute topology + GPU mesh emit + ExecuteIndirect";
    outPacket.NativeResourceApi = TerrainLodNativeResourceApi::Direct3D12;
    outPacket.NativeVertexBuffer = reinterpret_cast<std::uintptr_t>(frame.Vertices.Resource.Get());
    outPacket.NativeIndexBuffer = reinterpret_cast<std::uintptr_t>(frame.Indices.Resource.Get());
    outPacket.NativeIndirectDrawBuffer = reinterpret_cast<std::uintptr_t>(frame.IndirectArgs.Resource.Get());
    outPacket.GpuVertexBufferCapacityBytes = frame.Vertices.CapacityBytes;
    outPacket.GpuIndexBufferCapacityBytes = frame.Indices.CapacityBytes;
    outPacket.GpuResourceLifetime = TerrainLodGpuResourceLifetime::UntilNextBuildOrReset;
    outPacket.GpuResourceGeneration = ++_state->Generation;
    outPacket.ActiveLeafCount = activeEstimate;
    outPacket.ActiveTriangleCount = activeEstimate;
    outPacket.IndexCount = activeEstimate * 3U;

    const TerrainLodCpuSample cpuEnd = CaptureTerrainLodCpuSample();
    _stats.CpuUtilizationPercent = ComputeCpuUtilizationPercent(cpuStart, cpuEnd);
    return outPacket.HasConsistentResourceContract();
}

const TerrainLodStats& D3D12GpuRoamTerrainLodAlgorithm::Stats() const
{
    return _stats;
}

void D3D12GpuRoamTerrainLodAlgorithm::Reset()
{
    // 保留昂贵的 PSO 和缓冲容量，只清除拓扑历史与延迟统计
    _cpuTopologyBuilder = DataOrientedRoam::DataOrientedRoamMeshBuilder{};
    _stats = {};
    if (_state != nullptr)
    {
        _state->LastCounters = {};
        _state->LastGpuMilliseconds = 0.0F;
    }
}
} // namespace ParallelRoam::Algorithms::GpuRoam::D3D12
