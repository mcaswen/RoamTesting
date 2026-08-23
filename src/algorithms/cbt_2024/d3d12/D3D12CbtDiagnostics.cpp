#include "algorithms/cbt_2024/d3d12/D3D12CbtDiagnostics.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <vector>

namespace ParallelRoam::Algorithms::Cbt2024::D3D12
{
namespace
{
void SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
}

D3D12_HEAP_PROPERTIES ReadbackHeapProperties()
{
    // READBACK heap 对 CPU 可见，并且 D3D12 要求其初始状态固定为 COPY_DEST。
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = D3D12_HEAP_TYPE_READBACK;
    properties.CreationNodeMask = 1U;
    properties.VisibleNodeMask = 1U;
    return properties;
}

D3D12_RESOURCE_DESC ReadbackDescription()
{
    // 始终按最大验证载荷分配；普通帧仅映射计数头的有效范围。
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = D3D12CbtDiagnostics::ValidationReadbackBytes;
    description.Height = 1U;
    description.DepthOrArraySize = 1U;
    description.MipLevels = 1U;
    description.SampleDesc.Count = 1U;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return description;
}
} // namespace

static_assert(D3D12CbtDiagnostics::DiagnosticReadbackBytes == 112U);
static_assert(D3D12CbtDiagnostics::ValidationReadbackBytes == 304U);

bool D3D12CbtDiagnostics::Initialize(ID3D12Device* device, std::string* errorMessage)
{
    // 初始化保持全有或全无语义，失败不会留下部分可用的 readback ring。
    Shutdown();
    if (device == nullptr)
    {
        SetError(errorMessage, "CBT diagnostics requires a D3D12 device");
        return false;
    }
    const D3D12_HEAP_PROPERTIES heap = ReadbackHeapProperties();
    const D3D12_RESOURCE_DESC description = ReadbackDescription();
    for (Microsoft::WRL::ComPtr<ID3D12Resource>& readback : _readbacks)
    {
        if (FAILED(device->CreateCommittedResource(
                &heap,
                D3D12_HEAP_FLAG_NONE,
                &description,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(readback.ReleaseAndGetAddressOf()))))
        {
            SetError(errorMessage, "Failed to create CBT diagnostic readback buffer");
            Shutdown();
            return false;
        }
    }
    return true;
}

void D3D12CbtDiagnostics::Shutdown()
{
    // 清空资源和对应元数据，防止重建后消费旧 GPU 代次的期望值。
    _readbacks = {};
    _pending = {};
    _generations = {};
    _validationPending = {};
    _exactReferencePending = {};
    _expectations = {};
    _snapshot = {};
    _snapshot.IndexedActiveCount = CbtBaseBisectorCount;
    _faultMessage.clear();
    _faulted = false;
}

ID3D12Resource* D3D12CbtDiagnostics::Readback(std::uint32_t frameIndex) const
{
    return frameIndex < _readbacks.size() ? _readbacks[frameIndex].Get() : nullptr;
}

void D3D12CbtDiagnostics::QueueSample(
    std::uint32_t frameIndex,
    std::uint64_t generation,
    bool validation,
    bool exactReference,
    const D3D12CbtDiagnosticExpectation& expectation)
{
    // 调用点位于命令录制末尾，此处只把 CPU 参考绑定到同一个 staging 槽。
    _pending[frameIndex] = true;
    _generations[frameIndex] = generation;
    _validationPending[frameIndex] = validation;
    _exactReferencePending[frameIndex] = exactReference;
    _expectations[frameIndex] = expectation;
}

bool D3D12CbtDiagnostics::ConsumeCompleted(
    std::uint32_t frameIndex,
    std::string* errorMessage)
{
    if (!_pending[frameIndex])
    {
        return true;
    }

    // 普通诊断只读固定计数头；完整验证才会触碰 draw state 与基础节点数据。
    const std::size_t readbackBytes = _validationPending[frameIndex]
        ? ValidationReadbackBytes
        : DiagnosticReadbackBytes;
    const D3D12_RANGE readRange{0U, readbackBytes};
    void* mapped = nullptr;
    if (FAILED(_readbacks[frameIndex]->Map(0U, &readRange, &mapped)))
    {
        return LatchFault("Failed to map completed CBT E3 diagnostic counters", errorMessage);
    }
    // 四段计数保持原始 UAV 字节布局，避免为诊断再增加 GPU packing pass。
    const auto* bytes = static_cast<const std::uint8_t*>(mapped);
    const auto* counters = reinterpret_cast<const std::uint32_t*>(bytes + ClassificationCounterOffset);
    _snapshot.SplitCandidateCount = counters[0];
    _snapshot.SimplifyCandidateCount = counters[1];
    std::memcpy(
        &_snapshot.PlannedSplitNodeCount,
        bytes + AllocationCounterOffset,
        sizeof(_snapshot.PlannedSplitNodeCount));
    const auto* memoryCounters = reinterpret_cast<const std::uint32_t*>(bytes + MemoryCounterOffset);
    _snapshot.AllocatedSplitSlotCount = memoryCounters[0];
    _snapshot.RemainingDynamicSlotCount = memoryCounters[1];
    // validation[0..1] 是首错 code/slot，[2..11] 是无损诊断统计。
    std::array<std::uint32_t, CbtValidationWordCount> validation{};
    std::memcpy(validation.data(), bytes + ValidationCounterOffset, sizeof(validation));
    _snapshot.DuplicateSplitClaimCount = validation[2];
    _snapshot.SharedCompatibilityCount = validation[3];
    _snapshot.CompatibilityStepCount = validation[4];
    _snapshot.MaximumCompatibilityLength = validation[5];
    _snapshot.CommittedDynamicSlotCount = validation[6];
    _snapshot.SplitPropagationCount = validation[7];
    std::copy_n(
        validation.begin() + 8,
        _snapshot.BisectTemplateCounts.size(),
        _snapshot.BisectTemplateCounts.begin());
    const D3D12_RANGE noWrite{0U, 0U};
    // 先发布样本代次并释放槽位；任何失败随后都会锁存并阻止继续录制。
    _snapshot.SampleGeneration = _generations[frameIndex];
    _pending[frameIndex] = false;
    if (validation[0] != 0U)
    {
        const std::string message =
            "CBT E3 shader validation failed at generation " +
            std::to_string(_snapshot.SampleGeneration) + ": code/slot=" +
            std::to_string(validation[0]) + "/" + std::to_string(validation[1]);
        _readbacks[frameIndex]->Unmap(0U, &noWrite);
        return LatchFault(message, errorMessage);
    }
    // 活动统计属于常规诊断样本，不依赖昂贵的全拓扑验证开关。
    std::uint32_t occupancyRoot = 0U;
    CbtDrawState drawState{};
    std::memcpy(&occupancyRoot, bytes + OccupancyRootOffset, sizeof(occupancyRoot));
    std::memcpy(&drawState, bytes + DrawStateReadbackOffset, sizeof(drawState));
    _snapshot.ActiveDynamicSlotCount = occupancyRoot;
    _snapshot.IndexedActiveCount = drawState.ActiveBisectorCount;
    if (_snapshot.CommittedDynamicSlotCount != _snapshot.AllocatedSplitSlotCount ||
        drawState.Active.VertexCountPerInstance / 3U != drawState.ActiveBisectorCount ||
        drawState.ActiveBisectorCount != occupancyRoot + CbtBaseBisectorCount ||
        drawState.Visible.VertexCountPerInstance > drawState.Active.VertexCountPerInstance ||
        drawState.ModifiedPositionCount / 4U > drawState.ActiveBisectorCount)
    {
        const std::string message =
            "CBT E3 occupancy/indexation mismatch at generation " +
            std::to_string(_snapshot.SampleGeneration);
        _readbacks[frameIndex]->Unmap(0U, &noWrite);
        return LatchFault(message, errorMessage);
    }

    if (_validationPending[frameIndex])
    {
        _validationPending[frameIndex] = false;
        const bool exactReference = _exactReferencePending[frameIndex];
        _exactReferencePending[frameIndex] = false;
        if (exactReference)
        {
            // 精确参考只覆盖初始化事务，便于逐项定位 E1/E2/E3 接入错误。
            const D3D12CbtDiagnosticExpectation& expected = _expectations[frameIndex];
            if (_snapshot.SplitCandidateCount != expected.SplitCandidateCount ||
                _snapshot.SimplifyCandidateCount != expected.SimplifyCandidateCount)
            {
                const std::string message =
                    "CBT E1 CPU/GPU classification mismatch at generation " +
                    std::to_string(_snapshot.SampleGeneration) +
                    ": expected split/simplify=" + std::to_string(expected.SplitCandidateCount) + "/" +
                    std::to_string(expected.SimplifyCandidateCount) + ", GPU=" +
                    std::to_string(_snapshot.SplitCandidateCount) + "/" +
                    std::to_string(_snapshot.SimplifyCandidateCount);
                _readbacks[frameIndex]->Unmap(0U, &noWrite);
                return LatchFault(message, errorMessage);
            }
            if (_snapshot.PlannedSplitNodeCount != expected.PlannedSplitNodeCount ||
                _snapshot.AllocatedSplitSlotCount != expected.AllocatedSplitSlotCount ||
                _snapshot.RemainingDynamicSlotCount != expected.RemainingDynamicSlotCount)
            {
                const std::string message =
                    "CBT E2 planning counter mismatch at generation " +
                    std::to_string(_snapshot.SampleGeneration) +
                    ": expected nodes/slots/remaining=" + std::to_string(expected.PlannedSplitNodeCount) + "/" +
                    std::to_string(expected.AllocatedSplitSlotCount) + "/" +
                    std::to_string(expected.RemainingDynamicSlotCount) + ", GPU=" +
                    std::to_string(_snapshot.PlannedSplitNodeCount) + "/" +
                    std::to_string(_snapshot.AllocatedSplitSlotCount) + "/" +
                    std::to_string(_snapshot.RemainingDynamicSlotCount);
                _readbacks[frameIndex]->Unmap(0U, &noWrite);
                return LatchFault(message, errorMessage);
            }

            // 基础节点中的 subdivision pattern 和物理槽位用于验证四模板提交结果。
            std::array<CbtBisectorData, CbtBaseBisectorCount> baseData{};
            std::memcpy(baseData.data(), bytes + BaseBisectorDataOffset, sizeof(baseData));
            std::vector<std::uint32_t> allocatedSlots;
            for (std::size_t node = 0U; node < baseData.size(); ++node)
            {
                const std::uint32_t expectedPattern = expected.SubdivisionPatterns[node];
                if (baseData[node].SubdivisionPattern != expectedPattern)
                {
                    const std::string message =
                        "CBT E2 subdivision pattern mismatch at generation " +
                        std::to_string(_snapshot.SampleGeneration) + ", base node=" +
                        std::to_string(node) + ", expected=" + std::to_string(expectedPattern) +
                        ", GPU=" + std::to_string(baseData[node].SubdivisionPattern);
                    _readbacks[frameIndex]->Unmap(0U, &noWrite);
                    return LatchFault(message, errorMessage);
                }
                const std::uint32_t slotCount =
                    static_cast<std::uint32_t>(std::popcount(expectedPattern));
                for (std::uint32_t slot = 0U; slot < slotCount; ++slot)
                {
                    allocatedSlots.push_back(baseData[node].Indices[slot]);
                }
            }
            // 排序后可同时检查分配数、重复槽位以及旧 OCBT free-rank 的单调映射。
            std::sort(allocatedSlots.begin(), allocatedSlots.end());
            if (allocatedSlots.size() != expected.AllocatedSplitSlotCount ||
                std::adjacent_find(allocatedSlots.begin(), allocatedSlots.end()) != allocatedSlots.end())
            {
                _readbacks[frameIndex]->Unmap(0U, &noWrite);
                return LatchFault(
                    "CBT E2 allocated physical slots are incomplete or duplicated",
                    errorMessage);
            }
            for (std::uint32_t rank = 0U; rank < allocatedSlots.size(); ++rank)
            {
                if (allocatedSlots[rank] != rank)
                {
                    const std::string message =
                        "CBT E2 allocation did not use the old OCBT complement at free rank " +
                        std::to_string(rank);
                    _readbacks[frameIndex]->Unmap(0U, &noWrite);
                    return LatchFault(message, errorMessage);
                }
            }
            if (occupancyRoot != expected.AllocatedSplitSlotCount)
            {
                const std::string message =
                    "CBT E3 committed OCBT root mismatch: expected=" +
                    std::to_string(expected.AllocatedSplitSlotCount) + ", GPU=" +
                    std::to_string(occupancyRoot);
                _readbacks[frameIndex]->Unmap(0U, &noWrite);
                return LatchFault(message, errorMessage);
            }
        }
    }
    _readbacks[frameIndex]->Unmap(0U, &noWrite);
    return true;
}

bool D3D12CbtDiagnostics::LatchFault(const std::string& message, std::string* errorMessage)
{
    // 持久 GPU 拓扑无法局部回滚，因此只保留首错并交给算法层整体恢复。
    if (!_faulted)
    {
        _faultMessage = message;
        _faulted = true;
    }
    SetError(errorMessage, _faultMessage);
    return false;
}

const D3D12CbtDiagnosticSnapshot& D3D12CbtDiagnostics::Snapshot() const
{
    return _snapshot;
}

bool D3D12CbtDiagnostics::IsFaulted() const
{
    return _faulted;
}

const std::string& D3D12CbtDiagnostics::FaultMessage() const
{
    return _faultMessage;
}
} // namespace ParallelRoam::Algorithms::Cbt2024::D3D12
