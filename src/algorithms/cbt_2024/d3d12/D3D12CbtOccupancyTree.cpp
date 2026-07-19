#include "algorithms/cbt_2024/d3d12/D3D12CbtOccupancyTree.h"

#include "algorithms/cbt_2024/Cbt2024Support.h"
#include "algorithms/cbt_2024/CbtOccupancyTree.h"
#include "render/D3D12GraphicsBackend.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace ParallelRoam::Algorithms::Cbt2024::D3D12
{
namespace
{
constexpr std::uint32_t WorkGroupSize = 64U;
constexpr std::uint32_t ResultHeaderCount = 1U;

/// <summary>
/// GPU 更新缓冲中的单个位操作
/// </summary>
struct CbtBitUpdate
{
    std::uint32_t BitIndex{0U}; // 物理槽位索引
    std::uint32_t Occupied{0U}; // 非零表示置位
};

/// <summary>
/// 根常量控制更新批次和 rank-select 输出区间
/// </summary>
struct CbtTestConstants
{
    std::uint32_t UpdateOffset{0U}; // 当前批次在上传缓冲中的起点
    std::uint32_t UpdateCount{0U}; // 当前批次元素数量
    std::uint32_t DecodeCount{0U}; // 当前 rank-select 数量
    std::uint32_t ResultOffset{0U}; // 输出缓冲起点
};

static_assert(sizeof(CbtBitUpdate) == 8U);
static_assert(sizeof(CbtTestConstants) == 16U);

/// <summary>
/// 单个容量特化持有的 OCBT 测试管线和资源
/// </summary>
struct CbtGpuValidationState
{
    CbtOccupancyLayout Layout{}; // CPU 与 GPU 共享的容量布局
    Microsoft::WRL::ComPtr<ID3D12RootSignature> RootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ClearPipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ApplyPipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ReducePrePipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ReduceFirstPipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> ReduceSecondPipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> DecodeOccupiedPipeline;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> DecodeFreePipeline;
    Microsoft::WRL::ComPtr<ID3D12Resource> Tree;
    Microsoft::WRL::ComPtr<ID3D12Resource> Bitfield;
    Microsoft::WRL::ComPtr<ID3D12Resource> Updates;
    Microsoft::WRL::ComPtr<ID3D12Resource> Results;
    Microsoft::WRL::ComPtr<ID3D12Resource> Readback;
    std::uint8_t* MappedUpdates{nullptr}; // 上传堆生命周期内保持映射
    std::size_t ResultBytes{0U};

    ~CbtGpuValidationState()
    {
        if (Updates != nullptr && MappedUpdates != nullptr)
        {
            Updates->Unmap(0, nullptr);
        }
    }
};

using UpdateBatch = std::vector<CbtBitUpdate>;

void SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
}

D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type; // 调用点决定默认堆、上传堆或回读堆
    properties.CreationNodeMask = 1U; // 当前后端只创建单节点设备
    properties.VisibleNodeMask = 1U; // 资源只对节点零可见
    return properties;
}

D3D12_RESOURCE_DESC BufferDescription(std::size_t bytes, D3D12_RESOURCE_FLAGS flags)
{
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; // 所有 OCBT 资源都是线性缓冲
    description.Width = std::max<std::size_t>(bytes, 4U); // D3D12 拒绝零字节资源
    description.Height = 1U; // buffer 固定字段
    description.DepthOrArraySize = 1U; // buffer 固定字段
    description.MipLevels = 1U; // buffer 固定字段
    description.SampleDesc.Count = 1U; // buffer 固定字段
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; // buffer 必需布局
    description.Flags = flags; // 默认堆按需启用 UAV
    return description;
}

std::vector<std::uint8_t> ReadShader(const std::string& fileName, std::string* errorMessage)
{
#if defined(PARALLEL_ROAM_DX12_SHADER_DIR)
    const std::filesystem::path shaderDirectory{PARALLEL_ROAM_DX12_SHADER_DIR};
#else
    const std::filesystem::path shaderDirectory{"assets/shaders/dx12"};
#endif
    const std::filesystem::path path = shaderDirectory / fileName;
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (!stream)
    {
        SetError(errorMessage, "Failed to open CBT OCBT shader: " + path.string());
        return {};
    }

    const std::streamsize size = stream.tellg();
    if (size <= 0)
    {
        SetError(errorMessage, "CBT OCBT shader is empty: " + path.string());
        return {};
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        SetError(errorMessage, "Failed to read CBT OCBT shader: " + path.string());
        return {};
    }
    return bytes;
}

bool CreateDefaultBuffer(
    ID3D12Device* device,
    std::size_t bytes,
    Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
    std::string* errorMessage)
{
    const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT); // GPU 本地读写
    const D3D12_RESOURCE_DESC description =
        BufferDescription(bytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    if (FAILED(device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &description,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&resource))))
    {
        SetError(errorMessage, "Failed to allocate CBT OCBT default buffer");
        return false;
    }
    return true;
}

bool CreateMappedUploadBuffer(
    ID3D12Device* device,
    std::size_t bytes,
    Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
    std::uint8_t*& mapped,
    std::string* errorMessage)
{
    const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD); // CPU 持久写入更新序列
    const D3D12_RESOURCE_DESC description = BufferDescription(bytes, D3D12_RESOURCE_FLAG_NONE);
    if (FAILED(device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &description,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&resource))))
    {
        SetError(errorMessage, "Failed to allocate CBT OCBT update buffer");
        return false;
    }

    const D3D12_RANGE noRead{0U, 0U}; // CPU 不读取上传堆旧内容
    void* memory = nullptr;
    if (FAILED(resource->Map(0U, &noRead, &memory)))
    {
        resource.Reset();
        SetError(errorMessage, "Failed to map CBT OCBT update buffer");
        return false;
    }
    mapped = static_cast<std::uint8_t*>(memory);
    return true;
}

bool CreateReadbackBuffer(
    ID3D12Device* device,
    std::size_t bytes,
    Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
    std::string* errorMessage)
{
    const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_READBACK); // GPU 复制后由 CPU 验证
    const D3D12_RESOURCE_DESC description = BufferDescription(bytes, D3D12_RESOURCE_FLAG_NONE);
    if (FAILED(device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &description,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&resource))))
    {
        SetError(errorMessage, "Failed to allocate CBT OCBT readback buffer");
        return false;
    }
    return true;
}

bool CreateRootSignature(CbtGpuValidationState& state, ID3D12Device* device, std::string* errorMessage)
{
    // 根参数不依赖描述符堆，验证器可以在 renderer 初始化前独立运行
    std::array<D3D12_ROOT_PARAMETER, 5> parameters{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; // 更新和解码区间
    parameters[0].Constants.Num32BitValues = 4U; // 与 CbtTestConstants 精确对应
    parameters[0].Constants.ShaderRegister = 0U; // HLSL b0
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL; // compute 根签名统一可见性
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; // 压缩树根描述符
    parameters[1].Descriptor.ShaderRegister = 0U; // HLSL u0
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; // 64 位位域根描述符
    parameters[2].Descriptor.ShaderRegister = 1U; // HLSL u1
    parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; // 更新上传缓冲
    parameters[3].Descriptor.ShaderRegister = 0U; // HLSL t0
    parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; // rank-select 输出缓冲
    parameters[4].Descriptor.ShaderRegister = 2U; // HLSL u2

    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = static_cast<UINT>(parameters.size()); // 不使用静态 sampler
    description.pParameters = parameters.data(); // 序列化期间借用局部数组

    Microsoft::WRL::ComPtr<ID3DBlob> serialized;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT serializedResult = D3D12SerializeRootSignature(
        &description,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serialized,
        &errors);
    if (FAILED(serializedResult))
    {
        const char* detail = errors != nullptr
            ? static_cast<const char*>(errors->GetBufferPointer())
            : "unknown error";
        SetError(errorMessage, std::string{"Failed to serialize CBT OCBT root signature: "} + detail);
        return false;
    }

    if (FAILED(device->CreateRootSignature(
            0U,
            serialized->GetBufferPointer(),
            serialized->GetBufferSize(),
            IID_PPV_ARGS(&state.RootSignature))))
    {
        SetError(errorMessage, "Failed to create CBT OCBT root signature");
        return false;
    }
    return true;
}

bool CreatePipeline(
    CbtGpuValidationState& state,
    ID3D12Device* device,
    const std::string& shaderName,
    Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipeline,
    std::string* errorMessage)
{
    const std::vector<std::uint8_t> shader = ReadShader(shaderName, errorMessage);
    if (shader.empty())
    {
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
    description.pRootSignature = state.RootSignature.Get(); // 所有入口共享同一绑定协议
    description.CS = {shader.data(), shader.size()}; // 字节码生命周期覆盖 PSO 创建调用
    if (FAILED(device->CreateComputePipelineState(&description, IID_PPV_ARGS(&pipeline))))
    {
        SetError(errorMessage, "Failed to create CBT OCBT pipeline: " + shaderName);
        return false;
    }
    return true;
}

std::string ShaderPrefix(CbtOccupancyCapacity capacity)
{
    return std::string{"CbtOcbt"} + CbtOccupancyCapacityName(capacity);
}

bool InitializeState(
    CbtGpuValidationState& state,
    ID3D12Device* device,
    CbtOccupancyCapacity capacity,
    std::string* errorMessage)
{
    state.Layout = BuildCbtOccupancyLayout(capacity); // 资源大小和 dispatch 形状的唯一来源
    if (!CreateRootSignature(state, device, errorMessage))
    {
        return false;
    }

    const std::string prefix = ShaderPrefix(capacity);
    if (!CreatePipeline(state, device, prefix + "Clear.cso", state.ClearPipeline, errorMessage) ||
        !CreatePipeline(state, device, prefix + "ApplyUpdates.cso", state.ApplyPipeline, errorMessage) ||
        !CreatePipeline(state, device, prefix + "ReducePre.cso", state.ReducePrePipeline, errorMessage) ||
        !CreatePipeline(state, device, prefix + "ReduceFirst.cso", state.ReduceFirstPipeline, errorMessage) ||
        !CreatePipeline(state, device, prefix + "ReduceSecond.cso", state.ReduceSecondPipeline, errorMessage) ||
        !CreatePipeline(
            state,
            device,
            prefix + "DecodeOccupied.cso",
            state.DecodeOccupiedPipeline,
            errorMessage) ||
        !CreatePipeline(state, device, prefix + "DecodeFree.cso", state.DecodeFreePipeline, errorMessage))
    {
        return false;
    }

    const std::size_t treeBytes = state.Layout.TreeSlotCount * sizeof(std::uint32_t); // 压缩上层计数
    const std::size_t bitfieldBytes = state.Layout.BitfieldSlotCount * sizeof(std::uint64_t); // 原子占用位
    const std::size_t updateBytes = state.Layout.ElementCount * sizeof(CbtBitUpdate); // 满位图批次上限
    state.ResultBytes =
        (static_cast<std::size_t>(state.Layout.ElementCount) + ResultHeaderCount) * sizeof(std::uint32_t);
    return CreateDefaultBuffer(device, treeBytes, state.Tree, errorMessage) &&
           CreateDefaultBuffer(device, bitfieldBytes, state.Bitfield, errorMessage) &&
           CreateMappedUploadBuffer(device, updateBytes, state.Updates, state.MappedUpdates, errorMessage) &&
           CreateDefaultBuffer(device, state.ResultBytes, state.Results, errorMessage) &&
           CreateReadbackBuffer(device, state.ResultBytes, state.Readback, errorMessage);
}

std::uint32_t DispatchCount(std::uint32_t itemCount)
{
    return (itemCount + WorkGroupSize - 1U) / WorkGroupSize; // 向上取整且调用点保证非零
}

void UavBarrier(ID3D12GraphicsCommandList* commandList)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; // 建立跨入口写后读顺序
    barrier.UAV.pResource = nullptr; // 同时覆盖树、位域和结果缓冲
    commandList->ResourceBarrier(1U, &barrier);
}

void Transition(
    ID3D12GraphicsCommandList* commandList,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; // 结果缓冲在 UAV 和复制源间切换
    barrier.Transition.pResource = resource; // 仅转换测试结果资源
    barrier.Transition.StateBefore = before; // 调用方维护的已知前态
    barrier.Transition.StateAfter = after; // 后续命令要求的状态
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; // buffer 只有单一子资源
    commandList->ResourceBarrier(1U, &barrier);
}

void SetPass(
    ID3D12GraphicsCommandList* commandList,
    ID3D12PipelineState* pipeline,
    const CbtTestConstants& constants)
{
    commandList->SetPipelineState(pipeline); // 根参数在不同 compute PSO 间保持有效
    commandList->SetComputeRoot32BitConstants(0U, 4U, &constants, 0U); // 每次调度发布独立区间
}

bool ReadResults(
    CbtGpuValidationState& state,
    std::vector<std::uint32_t>& results,
    std::string* errorMessage)
{
    D3D12_RANGE readRange{0U, state.ResultBytes}; // 驱动只需同步当前结果范围
    void* mapped = nullptr;
    if (FAILED(state.Readback->Map(0U, &readRange, &mapped)))
    {
        SetError(errorMessage, "Failed to map CBT OCBT readback results");
        return false;
    }

    results.resize(state.Layout.ElementCount + ResultHeaderCount); // count 加活动和空闲完整排列
    std::memcpy(results.data(), mapped, state.ResultBytes);
    const D3D12_RANGE noWrite{0U, 0U}; // CPU 不修改回读堆
    state.Readback->Unmap(0U, &noWrite);
    return true;
}

bool CompareResults(
    const CbtGpuValidationState& state,
    const CbtOccupancyTree& reference,
    const std::string& caseName,
    const std::vector<std::uint32_t>& results,
    std::string* errorMessage)
{
    const std::vector<std::uint32_t> active = reference.ActiveIndices(); // CPU 位域升序活动列表
    const std::vector<std::uint32_t> free = reference.FreeIndices(); // CPU 位域升序空闲列表
    if (results[0] != active.size())
    {
        SetError(
            errorMessage,
            "CBT OCBT " + std::string{CbtOccupancyCapacityName(state.Layout.Capacity)} + " " + caseName +
                " active count mismatch");
        return false;
    }

    for (std::size_t rank = 0U; rank < active.size(); ++rank)
    {
        if (results[ResultHeaderCount + rank] != active[rank])
        {
            SetError(
                errorMessage,
                "CBT OCBT " + std::string{CbtOccupancyCapacityName(state.Layout.Capacity)} + " " + caseName +
                    " occupied rank mismatch at " + std::to_string(rank));
            return false;
        }
    }

    const std::size_t freeOffset = ResultHeaderCount + active.size(); // GPU 两类输出紧邻存储
    for (std::size_t rank = 0U; rank < free.size(); ++rank)
    {
        if (results[freeOffset + rank] != free[rank])
        {
            SetError(
                errorMessage,
                "CBT OCBT " + std::string{CbtOccupancyCapacityName(state.Layout.Capacity)} + " " + caseName +
                    " free rank mismatch at " + std::to_string(rank));
            return false;
        }
    }
    return true;
}

bool RunValidationCase(
    CbtGpuValidationState& state,
    Render::D3D12GraphicsBackend& backend,
    const std::string& caseName,
    const std::vector<UpdateBatch>& batches,
    std::string* errorMessage)
{
    CbtOccupancyTree reference{state.Layout.Capacity}; // 每个 case 从空树独立开始
    std::vector<CbtBitUpdate> flattenedUpdates; // GPU 上传保持批次间的命令顺序
    for (const UpdateBatch& batch : batches)
    {
        flattenedUpdates.insert(flattenedUpdates.end(), batch.begin(), batch.end());
        for (const CbtBitUpdate& update : batch)
        {
            if (!reference.SetBit(update.BitIndex, update.Occupied != 0U))
            {
                SetError(errorMessage, "CBT OCBT validation update is out of range");
                return false;
            }
        }
    }
    reference.Reduce();

    if (flattenedUpdates.size() > state.Layout.ElementCount)
    {
        SetError(errorMessage, "CBT OCBT validation update buffer capacity exceeded");
        return false;
    }
    if (!flattenedUpdates.empty())
    {
        std::memcpy(
            state.MappedUpdates,
            flattenedUpdates.data(),
            flattenedUpdates.size() * sizeof(CbtBitUpdate));
    }

    const std::uint32_t activeCount = reference.BitCount(); // 决定活动 rank 调度宽度
    const std::uint32_t freeCount = state.Layout.ElementCount - activeCount; // 决定空闲 rank 调度宽度
    if (!backend.ExecuteImmediate(
            [&](ID3D12GraphicsCommandList* commandList, std::string*) {
                commandList->SetComputeRootSignature(state.RootSignature.Get()); // 后续 PSO 共享根布局
                commandList->SetComputeRootUnorderedAccessView(1U, state.Tree->GetGPUVirtualAddress()); // u0
                commandList->SetComputeRootUnorderedAccessView(2U, state.Bitfield->GetGPUVirtualAddress()); // u1
                commandList->SetComputeRootShaderResourceView(3U, state.Updates->GetGPUVirtualAddress()); // t0
                commandList->SetComputeRootUnorderedAccessView(4U, state.Results->GetGPUVirtualAddress()); // u2

                CbtTestConstants constants{}; // clear 和 reduce 不消费区间字段
                SetPass(commandList, state.ClearPipeline.Get(), constants);
                commandList->Dispatch(
                    DispatchCount(std::max(state.Layout.TreeSlotCount, state.Layout.BitfieldSlotCount)),
                    1U,
                    1U);
                UavBarrier(commandList);

                std::uint32_t updateOffset = 0U; // 扁平上传中的当前批次起点
                for (const UpdateBatch& batch : batches)
                {
                    if (!batch.empty())
                    {
                        constants.UpdateOffset = updateOffset; // 保持 CPU 批次顺序
                        constants.UpdateCount = static_cast<std::uint32_t>(batch.size()); // 批内位互不重复
                        SetPass(commandList, state.ApplyPipeline.Get(), constants);
                        commandList->Dispatch(DispatchCount(constants.UpdateCount), 1U, 1U);
                        UavBarrier(commandList);
                    }
                    updateOffset += static_cast<std::uint32_t>(batch.size()); // 指向下一批上传前缀
                }

                constants = {};
                SetPass(commandList, state.ReducePrePipeline.Get(), constants);
                commandList->Dispatch(DispatchCount(state.Layout.LastTreeNodeCount), 1U, 1U);
                UavBarrier(commandList);

                SetPass(commandList, state.ReduceFirstPipeline.Get(), constants);
                commandList->Dispatch(state.Layout.SubtreeCount, 1U, 1U);
                UavBarrier(commandList);

                SetPass(commandList, state.ReduceSecondPipeline.Get(), constants);
                commandList->Dispatch(1U, 1U, 1U);
                UavBarrier(commandList);

                if (activeCount > 0U)
                {
                    constants.DecodeCount = activeCount; // 每个线程处理一个活动 rank
                    constants.ResultOffset = ResultHeaderCount; // results[0] 保留根计数
                    SetPass(commandList, state.DecodeOccupiedPipeline.Get(), constants);
                    commandList->Dispatch(DispatchCount(activeCount), 1U, 1U);
                }
                if (freeCount > 0U)
                {
                    constants.DecodeCount = freeCount; // 每个线程处理一个空闲 rank
                    constants.ResultOffset = ResultHeaderCount + activeCount; // 紧随活动列表
                    SetPass(commandList, state.DecodeFreePipeline.Get(), constants);
                    commandList->Dispatch(DispatchCount(freeCount), 1U, 1U);
                }
                UavBarrier(commandList);

                Transition(
                    commandList,
                    state.Results.Get(),
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_COPY_SOURCE);
                commandList->CopyBufferRegion(
                    state.Readback.Get(),
                    0U,
                    state.Results.Get(),
                    0U,
                    state.ResultBytes);
                Transition(
                    commandList,
                    state.Results.Get(),
                    D3D12_RESOURCE_STATE_COPY_SOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                return true;
            },
            errorMessage))
    {
        return false;
    }

    std::vector<std::uint32_t> results;
    if (!ReadResults(state, results, errorMessage) ||
        !CompareResults(state, reference, caseName, results, errorMessage))
    {
        return false;
    }

    std::cout << "CBT OCBT " << CbtOccupancyCapacityName(state.Layout.Capacity) << ' ' << caseName
              << ": active=" << activeCount << " free=" << freeCount << '\n';
    return true;
}

UpdateBatch BuildFullBatch(std::uint32_t elementCount)
{
    UpdateBatch batch(elementCount); // 满树用一次无冲突并行置位
    for (std::uint32_t bit = 0U; bit < elementCount; ++bit)
    {
        batch[bit] = CbtBitUpdate{bit, 1U};
    }
    return batch;
}

UpdateBatch BuildAlternatingBatch(std::uint32_t elementCount)
{
    UpdateBatch batch;
    batch.reserve(elementCount / 2U);
    for (std::uint32_t bit = 0U; bit < elementCount; bit += 2U)
    {
        batch.push_back(CbtBitUpdate{bit, 1U});
    }
    return batch;
}

std::vector<UpdateBatch> BuildRandomBatches(std::uint32_t elementCount)
{
    std::vector<std::uint32_t> indices(elementCount); // 洗牌后切分互不重复的更新集合
    std::iota(indices.begin(), indices.end(), 0U);
    std::mt19937 random{0xCB72024U + elementCount};
    std::shuffle(indices.begin(), indices.end(), random);

    const std::uint32_t firstSetCount = std::min<std::uint32_t>(elementCount / 8U, 32768U); // 初始占用集
    const std::uint32_t secondSetCount = std::min<std::uint32_t>(elementCount / 16U, 16384U); // 后续新增集
    UpdateBatch firstSet;
    UpdateBatch clearSubset;
    UpdateBatch secondSet;
    firstSet.reserve(firstSetCount);
    clearSubset.reserve(firstSetCount / 3U + 1U);
    secondSet.reserve(secondSetCount);
    for (std::uint32_t index = 0U; index < firstSetCount; ++index)
    {
        firstSet.push_back(CbtBitUpdate{indices[index], 1U});
        if (index % 3U == 0U)
        {
            clearSubset.push_back(CbtBitUpdate{indices[index], 0U});
        }
    }
    for (std::uint32_t index = 0U; index < secondSetCount; ++index)
    {
        secondSet.push_back(CbtBitUpdate{indices[firstSetCount + index], 1U});
    }
    return {std::move(firstSet), std::move(clearSubset), std::move(secondSet)};
}

bool ValidateCapacity(
    Render::D3D12GraphicsBackend& backend,
    CbtOccupancyCapacity capacity,
    std::string* errorMessage)
{
    CbtGpuValidationState state{};
    if (!InitializeState(state, backend.Device(), capacity, errorMessage))
    {
        return false;
    }

    const std::uint32_t elementCount = state.Layout.ElementCount;
    const UpdateBatch boundary{
        {0U, 1U},
        {1U, 1U},
        {63U, 1U},
        {64U, 1U},
        {65U, 1U},
        {127U, 1U},
        {elementCount - 2U, 1U},
        {elementCount - 1U, 1U},
    };
    return RunValidationCase(state, backend, "empty", {}, errorMessage) &&
           RunValidationCase(state, backend, "full", {BuildFullBatch(elementCount)}, errorMessage) &&
           RunValidationCase(state, backend, "boundary", {boundary}, errorMessage) &&
           RunValidationCase(
               state,
               backend,
               "alternating",
               {BuildAlternatingBatch(elementCount)},
               errorMessage) &&
           RunValidationCase(state, backend, "random-mutation", BuildRandomBatches(elementCount), errorMessage);
}
} // namespace

bool RunD3D12CbtOccupancyTreeSmokeTest(
    Render::D3D12GraphicsBackend& backend,
    std::string* errorMessage)
{
    const Cbt2024Availability availability = QueryCbt2024Availability(backend);
    if (!availability.Available)
    {
        SetError(errorMessage, availability.UnavailableReason);
        return false;
    }

    const std::array<CbtOccupancyCapacity, 4> capacities{
        CbtOccupancyCapacity::Capacity128K,
        CbtOccupancyCapacity::Capacity256K,
        CbtOccupancyCapacity::Capacity512K,
        CbtOccupancyCapacity::Capacity1M,
    };
    for (const CbtOccupancyCapacity capacity : capacities)
    {
        if (!ValidateCapacity(backend, capacity, errorMessage))
        {
            return false;
        }
    }

    std::cout << "CBT OCBT CPU/GPU validation passed for 128K, 256K, 512K and 1M\n";
    return true;
}
} // namespace ParallelRoam::Algorithms::Cbt2024::D3D12
