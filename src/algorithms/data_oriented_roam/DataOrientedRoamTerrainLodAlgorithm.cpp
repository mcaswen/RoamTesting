#include "algorithms/data_oriented_roam/DataOrientedRoamTerrainLodAlgorithm.h"

#include "algorithms/TerrainLodProfiling.h"

#include <algorithm>

namespace ParallelRoam::Algorithms::DataOrientedRoam
{
namespace
{
float ErrorEvaluationMilliseconds(const DataOrientedRoamStats& stats)
{
    return stats.ErrorEvaluationWorkerCount > 1U ?
        stats.ErrorEvaluationParallelMilliseconds :
        stats.ErrorEvaluationSingleThreadMilliseconds;
}
} // 匿名命名空间

// adapter 保持和 Classic adapter 相同的接口形状
// 差异只在内部 builder 的 SoA node pool 表达
// benchmark 因此可以在同一 profile 下直接比较 Classic 与 DOD
TerrainLodAlgorithmInfo DataOrientedRoamTerrainLodAlgorithm::Info() const
{
    return TerrainLodAlgorithmInfo{
        TerrainLodAlgorithmId::DataOrientedCpuRoam,
        "data_oriented_cpu_roam",
        "Data-Oriented CPU ROAM",
        "SoA CPU ROAM with batched screen-error evaluation",
    };
}

TerrainLodAlgorithmCapabilities DataOrientedRoamTerrainLodAlgorithm::Capabilities() const
{
    TerrainLodAlgorithmCapabilities capabilities{};
    capabilities.SupportsCpuMeshOutput = true;
    capabilities.SupportsSplit = true;
    capabilities.SupportsMerge = true;
    capabilities.SupportsCrackFix = true;
    capabilities.SupportsTopologyValidation = true;
    return capabilities;
}

bool DataOrientedRoamTerrainLodAlgorithm::BuildRenderData(
    const TerrainLodBuildInput& input,
    TerrainLodRenderPacket& outPacket,
    std::string* errorMessage)
{
    _stats = {};
    outPacket = {};

    // 无效 HeightMap 在算法边界上直接失败
    // 避免 benchmark 把空 mesh 当作合法低细节输出
    if (input.HeightMap == nullptr || !input.HeightMap->IsValid())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Data-Oriented CPU ROAM build failed: invalid height map";
        }
        return false;
    }

    // 当前路径仍然输出 CPU mesh
    // 并行误差评估不改变 renderer 消费方式
    outPacket.Mode = TerrainLodRenderMode::CpuMesh;
    const TerrainLodCpuSample cpuSampleStart = CaptureTerrainLodCpuSample();
    outPacket.CpuMesh = _builder.Build(
        *input.HeightMap,
        input.Settings.TerrainSize,
        input.Settings.HeightScale,
        input.CameraPosition,
        ToDataOrientedSettings(input.Settings));
    const TerrainLodCpuSample cpuSampleEnd = CaptureTerrainLodCpuSample();
    _stats = ToTerrainLodStats(_builder.Stats());
    _stats.CpuUtilizationPercent = ComputeCpuUtilizationPercent(cpuSampleStart, cpuSampleEnd);
    outPacket.ActiveTriangleCount = _stats.ActiveTriangleCount;
    outPacket.IndexCount = outPacket.CpuMesh.Indices.size();
    return !outPacket.CpuMesh.Vertices.empty() && !outPacket.CpuMesh.Indices.empty();
}

const TerrainLodStats& DataOrientedRoamTerrainLodAlgorithm::Stats() const
{
    return _stats;
}

void DataOrientedRoamTerrainLodAlgorithm::Reset()
{
    // Reset 丢弃 index pool 和 hysteresis path
    // 下一帧会重新建立 root diamond
    _builder = DataOrientedRoamMeshBuilder{};
    _stats = {};
}

DataOrientedRoamSettings DataOrientedRoamTerrainLodAlgorithm::ToDataOrientedSettings(
    const TerrainLodSettings& settings)
{
    // DOD 使用与 Classic 相同的控制变量
    // 这是三版本 benchmark 可比性的前提
    DataOrientedRoamSettings dataSettings{};
    dataSettings.MaxDepth = settings.MaxDepth;
    dataSettings.SplitThreshold = settings.SplitThreshold;
    dataSettings.MergeThreshold = settings.MergeThreshold;
    dataSettings.DistanceScale = settings.DistanceScale;
    dataSettings.SplitBudget = settings.SplitBudget;
    // worker 数保持 DOD 内部策略  避免扩大统一参数面
    dataSettings.ErrorEvaluationWorkerCount = 0U;
    dataSettings.EnableLocalConstraints = settings.EnableLocalConstraints;
    dataSettings.EnableTopologyValidation = settings.EnableTopologyValidation;
    return dataSettings;
}

TerrainLodStats DataOrientedRoamTerrainLodAlgorithm::ToTerrainLodStats(const DataOrientedRoamStats& stats)
{
    // DOD 私有统计映射到统一字段
    // CSV 不暴露具体 node pool 实现细节
    TerrainLodStats lodStats{};
    lodStats.ActiveTriangleCount = stats.ActiveTriangleCount;
    lodStats.ActiveNodeCount = stats.NodeCount;
    lodStats.OriginalTriangleCount = stats.OriginalTriangleCount;
    lodStats.SubdividedTriangleCount = stats.SubdividedTriangleCount;
    lodStats.RebuiltTriangleCount = stats.RebuiltTriangleCount;
    lodStats.ActiveSplitCount = stats.ActiveSplitCount;
    lodStats.SplitCount = stats.SplitCount;
    lodStats.ForcedSplitCount = stats.ForcedSplitCount;
    lodStats.MergeCount = stats.MergeCount;
    lodStats.CrackRiskCount = stats.CrackRiskCount;
    lodStats.ConstraintPassCount = stats.ConstraintPassCount;
    lodStats.CandidatePeakCount = stats.CandidatePeakCount;
    lodStats.RejectedSplitCount = stats.RejectedSplitCount;
    lodStats.RejectedMergeCount = stats.RejectedMergeCount;
    lodStats.TjunctionCount = stats.TjunctionCount;
    lodStats.InvalidNeighborCount = stats.InvalidNeighborCount;
    lodStats.InvalidTopologyCount = stats.InvalidTopologyCount;
    lodStats.CpuWorkerCount = std::max(std::size_t{1}, stats.ErrorEvaluationWorkerCount);
    // error eval 从 split pass 里拆出独立 CSV 字段
    const float errorEvaluationMilliseconds = ErrorEvaluationMilliseconds(stats);
    lodStats.CpuUpdateMilliseconds = stats.UpdateMilliseconds;
    lodStats.CpuErrorEvalMilliseconds = errorEvaluationMilliseconds;
    lodStats.CpuDecisionMilliseconds = std::max(0.0F, stats.SplitMilliseconds - errorEvaluationMilliseconds);
    lodStats.CpuTopologyMilliseconds = lodStats.CpuDecisionMilliseconds + stats.MergeMilliseconds;
    lodStats.CpuMeshBuildMilliseconds = stats.EmitMilliseconds;
    lodStats.SplitMilliseconds = stats.SplitMilliseconds;
    lodStats.MergeMilliseconds = stats.MergeMilliseconds;
    lodStats.EmitMilliseconds = stats.EmitMilliseconds;
    lodStats.ValidateMilliseconds = stats.ValidateMilliseconds;
    lodStats.MaxActiveDepth = stats.MaxDepthReached;
    return lodStats;
}
} // 命名空间 ParallelRoam::Algorithms::DataOrientedRoam
