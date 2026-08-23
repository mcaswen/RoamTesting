#include "algorithms/cbt_2024/d3d12/D3D12CbtTerrainLodAlgorithm.h"

#include "algorithms/cbt_2024/Cbt2024Support.h"
#include "algorithms/cbt_2024/d3d12/D3D12CbtBaseTopology.h"
#include "algorithms/cbt_2024/d3d12/D3D12CbtE0Pipeline.h"
#include "render/D3D12GraphicsBackend.h"
#include "terrain/TerrainMeshBuilder.h"
#include "tools/PerformanceTimer.h"

#include <d3d12.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace ParallelRoam::Algorithms::Cbt2024::D3D12
{
struct D3D12CbtTerrainState
{
    // Topology 必须晚于 Pipeline 析构；成员逆序销毁保证描述符先归还再释放资源
    D3D12CbtBaseTopologyState Topology;
    D3D12CbtE0Pipeline Pipeline;
    float TerrainSize{0.0F};
    bool GeometryInitialized{false};
};

namespace
{
void SetError(std::string* errorMessage, const std::string& message)
{
    if (errorMessage != nullptr)
    {
        *errorMessage = message;
    }
}

void FillRenderPacket(
    const D3D12CbtTerrainState& state,
    TerrainLodRenderPacket& outPacket)
{
    const CbtBaseTopology& topology = state.Topology.Topology();
    const D3D12CbtTopologyResourceView resources = state.Topology.Resources();
    const auto& templates = state.Pipeline.LastBisectTemplateCounts();
    outPacket.Mode = TerrainLodRenderMode::GpuProceduralIndirect;
    outPacket.StatusMessage =
        "CBT 2024 E3 commit: split=" + std::to_string(state.Pipeline.LastSplitCandidateCount()) +
        " simplify=" + std::to_string(state.Pipeline.LastSimplifyCandidateCount()) +
        " nodes=" + std::to_string(state.Pipeline.LastPlannedSplitNodeCount()) +
        " slots=" + std::to_string(state.Pipeline.LastAllocatedSplitSlotCount()) +
        " committed=" + std::to_string(state.Pipeline.LastCommittedDynamicSlotCount()) +
        " propagate=" + std::to_string(state.Pipeline.LastSplitPropagationCount()) +
        " templates=" + std::to_string(templates[0]) + "/" +
        std::to_string(templates[1]) + "/" + std::to_string(templates[2]) + "/" +
        std::to_string(templates[3]) +
        " active=" + std::to_string(state.Pipeline.LastIndexedActiveCount()) +
        " remaining=" + std::to_string(state.Pipeline.LastRemainingDynamicSlotCount()) +
        " duplicate/shared=" + std::to_string(state.Pipeline.LastDuplicateSplitClaimCount()) + "/" +
        std::to_string(state.Pipeline.LastSharedCompatibilityCount()) +
        " chain=" + std::to_string(state.Pipeline.LastCompatibilityStepCount()) + "/" +
        std::to_string(state.Pipeline.LastMaximumCompatibilityLength()) +
        " sample=" + std::to_string(state.Pipeline.ClassificationSampleGeneration());
    outPacket.NativeResourceApi = TerrainLodNativeResourceApi::Direct3D12;
    outPacket.NativeVertexBuffer = reinterpret_cast<std::uintptr_t>(state.Pipeline.RenderVertices());
    outPacket.NativeActiveLeafBuffer = reinterpret_cast<std::uintptr_t>(resources.ActiveIndices);
    outPacket.NativeIndirectDrawBuffer = reinterpret_cast<std::uintptr_t>(resources.IndirectDrawState);
    outPacket.GpuVertexBufferCapacityBytes = state.Pipeline.RenderVertexCapacityBytes();
    outPacket.GpuVertexStrideBytes = sizeof(Terrain::TerrainMeshVertex);
    outPacket.GpuActiveLeafBufferCapacityBytes =
        static_cast<std::size_t>(topology.Layout.IndexElementCount) * sizeof(std::uint32_t);
    outPacket.GpuActiveLeafStrideBytes = sizeof(std::uint32_t);
    outPacket.GpuIndirectDrawBufferCapacityBytes = sizeof(CbtDrawState);
    outPacket.GpuIndirectDrawArgumentOffsetBytes = offsetof(CbtDrawState, Active);
    outPacket.GpuResourceLifetime = TerrainLodGpuResourceLifetime::UntilNextBuildOrReset;
    outPacket.GpuResourceGeneration = state.Topology.Generation();
    // 数量来自延迟诊断镜像；绘制正确性仍只依赖 GPU draw state。
    outPacket.ActiveLeafCount = state.Pipeline.LastIndexedActiveCount();
    outPacket.ActiveTriangleCount = state.Pipeline.LastIndexedActiveCount();
}
} // namespace

D3D12CbtTerrainLodAlgorithm::D3D12CbtTerrainLodAlgorithm(Render::D3D12GraphicsBackend& backend)
    : _backend(&backend),
      _state(std::make_unique<D3D12CbtTerrainState>())
{
}

D3D12CbtTerrainLodAlgorithm::~D3D12CbtTerrainLodAlgorithm() = default;

TerrainLodAlgorithmInfo D3D12CbtTerrainLodAlgorithm::Info() const
{
    return TerrainLodAlgorithmInfo{
        TerrainLodAlgorithmId::Cbt2024,
        "cbt-2024-e3",
        "CBT 2024（E3）",
        "GPU 常驻拓扑、兼容链规划、四模板 Bisect、邻接传播、OCBT 提交和增量几何",
    };
}

TerrainLodAlgorithmCapabilities D3D12CbtTerrainLodAlgorithm::Capabilities() const
{
    return TerrainLodAlgorithmCapabilities{
        .SupportsCpuMeshOutput = false,
        .SupportsGpuDrivenRendering = true,
        .SupportsProceduralIndirectRendering = true,
        .SupportsSplit = true,
        .SupportsMerge = false,
        .SupportsCrackFix = true,
        .SupportsTopologyValidation = true,
        .RequiresShaderModel66 = true,
        .RequiresInt64ShaderOps = true,
        .RequiresInt64Atomics = true,
        .UpdatePolicy = TerrainLodUpdatePolicy::EveryFrame,
    };
}

bool D3D12CbtTerrainLodAlgorithm::BuildRenderData(
    const TerrainLodBuildInput& input,
    TerrainLodRenderPacket& outPacket,
    std::string* errorMessage)
{
    Tools::PerformanceTimer buildTimer;
    outPacket = {};
    _stats = {};

    if (_backend == nullptr || _backend->Device() == nullptr)
    {
        SetError(errorMessage, "CBT 2024 requires an initialized D3D12 device");
        return false;
    }
    if (!_backend->FrameOpen() || _backend->CommandList() == nullptr)
    {
        SetError(errorMessage, "CBT 2024 E3 must record between BeginFrame and Present");
        return false;
    }
    const Cbt2024Availability availability = QueryCbt2024Availability(*_backend);
    if (!availability.Available)
    {
        SetError(errorMessage, availability.UnavailableReason);
        return false;
    }
    if (input.HeightMap == nullptr || !input.HeightMap->IsValid())
    {
        SetError(errorMessage, "CBT 2024 E3 requires a valid terrain input");
        return false;
    }

    if (!_state->Topology.IsInitialized())
    {
        if (!_state->Topology.Rebuild(
                *_backend,
                CbtOccupancyCapacity::Capacity128K,
                errorMessage))
        {
            return false;
        }
    }
    if (!_state->Pipeline.IsInitialized())
    {
        if (!_state->Pipeline.Initialize(
                *_backend,
                _state->Topology.Topology(),
                _state->Topology.Resources(),
                errorMessage))
        {
            return false;
        }
    }

    const bool rebuildGeometry =
        !_state->GeometryInitialized || _state->TerrainSize != input.Settings.TerrainSize;
    if (!_state->Pipeline.RecordFrame(
            input,
            _state->Topology.Topology(),
            _state->Topology.Resources(),
            rebuildGeometry,
            errorMessage))
    {
        return false;
    }
    if (rebuildGeometry)
    {
        _state->TerrainSize = input.Settings.TerrainSize;
        _state->GeometryInitialized = true;
    }

    const std::size_t activeCount = _state->Pipeline.LastIndexedActiveCount();
    const std::size_t committedNodeCount = _state->Pipeline.LastPlannedSplitNodeCount();
    _stats.ActiveTriangleCount = activeCount;
    _stats.GpuTopologyFrameGeneration = _state->Pipeline.TopologyFrameGeneration();
    _stats.GpuClassificationSampleGeneration = _state->Pipeline.ClassificationSampleGeneration();
    _stats.ActiveNodeCount = activeCount;
    _stats.OriginalTriangleCount = CbtBaseBisectorCount;
    _stats.SubdividedTriangleCount = activeCount > CbtBaseBisectorCount
        ? activeCount - CbtBaseBisectorCount
        : 0U;
    _stats.RebuiltTriangleCount =
        committedNodeCount + _state->Pipeline.LastCommittedDynamicSlotCount();
    _stats.ActiveSplitCount = committedNodeCount;
    _stats.SplitCount = committedNodeCount;
    _stats.ForcedSplitCount = committedNodeCount > _state->Pipeline.LastSplitCandidateCount()
        ? committedNodeCount - _state->Pipeline.LastSplitCandidateCount()
        : 0U;
    _stats.ConstraintPassCount = _state->Pipeline.LastSplitPropagationCount();
    _stats.CandidatePeakCount = _state->Pipeline.LastSplitCandidateCount() +
        _state->Pipeline.LastSimplifyCandidateCount();
    _stats.SplitTopologyCandidateCount = _state->Pipeline.LastSplitCandidateCount();
    _stats.MergeTopologyCandidateCount = _state->Pipeline.LastSimplifyCandidateCount();
    _stats.CbtCommittedDynamicSlotCount = _state->Pipeline.LastCommittedDynamicSlotCount();
    _stats.CbtSplitPropagationCount = _state->Pipeline.LastSplitPropagationCount();
    const auto& templateCounts = _state->Pipeline.LastBisectTemplateCounts();
    for (std::size_t index = 0U; index < templateCounts.size(); ++index)
    {
        _stats.CbtBisectTemplateCounts[index] = templateCounts[index];
    }
    _stats.MaxActiveDepth = static_cast<int>(_state->Topology.Topology().BaseDepth);
    _stats.CpuGpuReadbackBytes = input.Settings.EnableTopologyValidation ? 304U : 68U;
    _stats.CpuUpdateMilliseconds = buildTimer.Stop();

    FillRenderPacket(*_state, outPacket);
    if (!outPacket.HasConsistentResourceContract())
    {
        SetError(errorMessage, "CBT 2024 E3 produced an inconsistent GPU render packet");
        return false;
    }
    if (errorMessage != nullptr)
    {
        errorMessage->clear();
    }
    return true;
}

const TerrainLodStats& D3D12CbtTerrainLodAlgorithm::Stats() const
{
    return _stats;
}

void D3D12CbtTerrainLodAlgorithm::Reset()
{
    // 资源常驻；renderer 在真正销毁算法前负责等待其最后一个 fence
    _stats = {};
}
} // namespace ParallelRoam::Algorithms::Cbt2024::D3D12
