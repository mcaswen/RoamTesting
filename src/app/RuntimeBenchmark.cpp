#include "app/RuntimeBenchmark.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ParallelRoam::App
{
namespace
{
struct RuntimeBenchmarkSummary
{
    // 汇总表只保留横向比较最常用的核心指标
    std::size_t SampleCount{0};

    // Frame ms 代表整帧体验，包含算法外的渲染和 UI 成本
    float AverageFrameMilliseconds{0.0F};
    float MaxFrameMilliseconds{0.0F};

    // Total LOD 覆盖算法更新到 renderer 输出可绘制数据的完整边界
    float AverageTotalLodMilliseconds{0.0F};
    float MaxTotalLodMilliseconds{0.0F};
    float AverageCpuUpdateMilliseconds{0.0F};
    float AverageCpuPrepareMilliseconds{0.0F};
    float AverageCpuMergeCandidateMarkMilliseconds{0.0F};
    float AverageCpuMergeTopologyMilliseconds{0.0F};
    float AverageCpuBudgetLeafCollectMilliseconds{0.0F};
    float AverageCpuErrorEvalMilliseconds{0.0F};
    float AverageCpuSplitCandidateMarkMilliseconds{0.0F};
    float AverageCpuSplitTopologyMilliseconds{0.0F};
    float AverageCpuFinalLeafCollectMilliseconds{0.0F};
    float AverageCpuMeshEmitMilliseconds{0.0F};
    float AverageCpuFinalizeMilliseconds{0.0F};
    float AverageCpuUploadMilliseconds{0.0F};
    float AverageSplitMilliseconds{0.0F};
    float AverageMergeMilliseconds{0.0F};
    float AverageEmitMilliseconds{0.0F};
    float AverageValidateMilliseconds{0.0F};
    float AverageGpuInitialLeafCompactionMilliseconds{0.0F};
    float AverageGpuErrorEvaluationMilliseconds{0.0F};
    float AverageGpuSplitCandidateMarkingMilliseconds{0.0F};
    float AverageGpuMergeCandidateMarkingMilliseconds{0.0F};
    float AverageGpuSplitTopologyMilliseconds{0.0F};
    float AverageGpuActiveLeafResetMilliseconds{0.0F};
    float AverageGpuFinalLeafCompactionMilliseconds{0.0F};
    float AverageGpuMeshEmitMilliseconds{0.0F};
    float AverageGpuPassSumMilliseconds{0.0F};
    float MaxGpuPassSumMilliseconds{0.0F};
    float AverageGpuSnapshotBuildMilliseconds{0.0F};
    float AverageGpuBufferAllocationMilliseconds{0.0F};
    float AverageGpuDispatchWallMilliseconds{0.0F};
    float AverageGpuQueryWaitMilliseconds{0.0F};
    float AverageGpuReadbackWaitMilliseconds{0.0F};
    float AverageFrameFenceWaitMilliseconds{0.0F};
    float MaxFrameFenceWaitMilliseconds{0.0F};
    float AverageRenderMilliseconds{0.0F};
    float MaxRenderMilliseconds{0.0F};

    // 三角形数量是画面复杂度和 GPU 提交压力的共同代理
    double AverageTriangles{0.0};
    std::size_t MaxTriangles{0};

    // 节点数量体现拓扑状态规模，和三角形数量不总是线性对应
    double AverageNodes{0.0};
    std::size_t MaxNodes{0};

    // CPU 百分比按单核 100% 口径，适合观察并行扩展
    float AverageCpuUtilizationPercent{0.0F};
    float MaxCpuUtilizationPercent{0.0F};

    // Worker 数记录算法本帧实际使用的 CPU 并行宽度
    std::size_t MaxCpuWorkerCount{0};
    std::size_t MaxCpuGpuUploadBytes{0};
    std::size_t MaxCpuGpuReadbackBytes{0};

    // 配置深度和实际达到深度分开，避免把 UI 设置误读成结果
    int MaxDepthSetting{0};
    int MaxDepthReached{0};

    // 拓扑问题合并输出，汇总表不用展开三类错误
    std::size_t MaxInvalidTopologyCount{0};
};

std::tm ToLocalTime(std::time_t timestamp)
{
    // localtime_r/localtime_s 避免使用带静态存储的 localtime
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &timestamp);
#else
    localtime_r(&timestamp, &localTime);
#endif
    return localTime;
}

std::string MakeReportTimestamp()
{
    // 时间戳放进文件名，连续多次点击 benchmark 不会覆盖旧结果
    const auto now = std::chrono::system_clock::now();
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
    const std::tm localTime = ToLocalTime(timestamp);

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y%m%d-%H%M%S");
    return stream.str();
}

const Render::TerrainRenderStats* FindFirstSampleStats(
    const std::vector<RuntimeBenchmarkAlgorithmResult>& results)
{
    // 同一轮 benchmark 中算法共享地形和 UI 参数
    // 首个样本足够代表本轮配置
    for (const RuntimeBenchmarkAlgorithmResult& result : results)
    {
        if (!result.Samples.empty())
        {
            return &result.Samples.front().Stats;
        }
    }

    return nullptr;
}

RuntimeBenchmarkSummary SummarizeRuntimeBenchmark(const RuntimeBenchmarkAlgorithmResult& result)
{
    RuntimeBenchmarkSummary summary{};
    summary.SampleCount = result.Samples.size();
    if (summary.SampleCount == 0U)
    {
        // 空结果仍输出一行，方便发现某个算法没有采到样本
        return summary;
    }

    // 累加使用 double，避免长时间采样后平均值被 float 精度吞掉
    double totalFrameMilliseconds = 0.0;
    double totalLodMilliseconds = 0.0;
    double totalCpuUpdateMilliseconds = 0.0;
    double totalCpuPrepareMilliseconds = 0.0;
    double totalCpuMergeCandidateMarkMilliseconds = 0.0;
    double totalCpuMergeTopologyMilliseconds = 0.0;
    double totalCpuBudgetLeafCollectMilliseconds = 0.0;
    double totalCpuErrorEvalMilliseconds = 0.0;
    double totalCpuSplitCandidateMarkMilliseconds = 0.0;
    double totalCpuSplitTopologyMilliseconds = 0.0;
    double totalCpuFinalLeafCollectMilliseconds = 0.0;
    double totalCpuMeshEmitMilliseconds = 0.0;
    double totalCpuFinalizeMilliseconds = 0.0;
    double totalCpuUploadMilliseconds = 0.0;
    double totalSplitMilliseconds = 0.0;
    double totalMergeMilliseconds = 0.0;
    double totalEmitMilliseconds = 0.0;
    double totalValidateMilliseconds = 0.0;
    double totalGpuInitialLeafCompactionMilliseconds = 0.0;
    double totalGpuErrorEvaluationMilliseconds = 0.0;
    double totalGpuSplitCandidateMarkingMilliseconds = 0.0;
    double totalGpuMergeCandidateMarkingMilliseconds = 0.0;
    double totalGpuSplitTopologyMilliseconds = 0.0;
    double totalGpuActiveLeafResetMilliseconds = 0.0;
    double totalGpuFinalLeafCompactionMilliseconds = 0.0;
    double totalGpuMeshEmitMilliseconds = 0.0;
    double totalGpuPassSumMilliseconds = 0.0;
    double totalGpuSnapshotBuildMilliseconds = 0.0;
    double totalGpuBufferAllocationMilliseconds = 0.0;
    double totalGpuDispatchWallMilliseconds = 0.0;
    double totalGpuQueryWaitMilliseconds = 0.0;
    double totalGpuReadbackWaitMilliseconds = 0.0;
    double totalFrameFenceWaitMilliseconds = 0.0;
    double totalRenderMilliseconds = 0.0;
    double totalTriangles = 0.0;
    double totalNodes = 0.0;
    double totalCpuUtilization = 0.0;

    for (const RuntimeBenchmarkSample& sample : result.Samples)
    {
        const Render::TerrainRenderStats& stats = sample.Stats;
        // 三类拓扑问题合并成表格中的一个核心风险指标
        const std::size_t invalidTopologyCount =
            stats.RoamTjunctionCount + stats.RoamInvalidNeighborCount + stats.RoamInvalidTopologyCount;

        // 平均值用于整体对比，最大值用于定位尖峰卡顿
        totalFrameMilliseconds += sample.FrameMilliseconds;
        totalLodMilliseconds += stats.RoamTotalMilliseconds;
        totalCpuUpdateMilliseconds += stats.RoamUpdateMilliseconds;
        totalCpuPrepareMilliseconds += stats.RoamCpuPrepareMilliseconds;
        totalCpuMergeCandidateMarkMilliseconds += stats.RoamCpuMergeCandidateMarkMilliseconds;
        totalCpuMergeTopologyMilliseconds += stats.RoamCpuMergeTopologyMilliseconds;
        totalCpuBudgetLeafCollectMilliseconds += stats.RoamCpuBudgetLeafCollectMilliseconds;
        totalCpuErrorEvalMilliseconds += stats.RoamCpuErrorEvalMilliseconds;
        totalCpuSplitCandidateMarkMilliseconds += stats.RoamCpuSplitCandidateMarkMilliseconds;
        totalCpuSplitTopologyMilliseconds += stats.RoamCpuSplitTopologyMilliseconds;
        totalCpuFinalLeafCollectMilliseconds += stats.RoamCpuFinalLeafCollectMilliseconds;
        totalCpuMeshEmitMilliseconds += stats.RoamCpuMeshEmitMilliseconds;
        totalCpuFinalizeMilliseconds += stats.RoamCpuFinalizeMilliseconds;
        totalCpuUploadMilliseconds += stats.RoamCpuUploadMilliseconds;
        totalSplitMilliseconds += stats.RoamSplitMilliseconds;
        totalMergeMilliseconds += stats.RoamMergeMilliseconds;
        totalEmitMilliseconds += stats.RoamEmitMilliseconds;
        totalValidateMilliseconds += stats.RoamValidateMilliseconds;
        totalGpuInitialLeafCompactionMilliseconds += stats.RoamGpuInitialLeafCompactionMilliseconds;
        totalGpuErrorEvaluationMilliseconds += stats.RoamGpuErrorEvaluationMilliseconds;
        totalGpuSplitCandidateMarkingMilliseconds += stats.RoamGpuSplitCandidateMarkingMilliseconds;
        totalGpuMergeCandidateMarkingMilliseconds += stats.RoamGpuMergeCandidateMarkingMilliseconds;
        totalGpuSplitTopologyMilliseconds += stats.RoamGpuSplitTopologyMilliseconds;
        totalGpuActiveLeafResetMilliseconds += stats.RoamGpuActiveLeafResetMilliseconds;
        totalGpuFinalLeafCompactionMilliseconds += stats.RoamGpuFinalLeafCompactionMilliseconds;
        totalGpuMeshEmitMilliseconds += stats.RoamGpuMeshEmitMilliseconds;
        totalGpuPassSumMilliseconds += stats.RoamGpuPassSumMilliseconds;
        totalGpuSnapshotBuildMilliseconds += stats.RoamGpuSnapshotBuildMilliseconds;
        totalGpuBufferAllocationMilliseconds += stats.RoamGpuBufferAllocationMilliseconds;
        totalGpuDispatchWallMilliseconds += stats.RoamGpuDispatchWallMilliseconds;
        totalGpuQueryWaitMilliseconds += stats.RoamGpuQueryWaitMilliseconds;
        totalGpuReadbackWaitMilliseconds += stats.RoamGpuReadbackWaitMilliseconds;
        totalFrameFenceWaitMilliseconds += stats.RoamFrameFenceWaitMilliseconds;
        totalRenderMilliseconds += stats.RoamRenderMilliseconds;
        totalTriangles += static_cast<double>(stats.TriangleCount);
        totalNodes += static_cast<double>(stats.RoamNodeCount);
        totalCpuUtilization += stats.RoamCpuUtilizationPercent;

        summary.MaxFrameMilliseconds = std::max(summary.MaxFrameMilliseconds, sample.FrameMilliseconds);
        summary.MaxTotalLodMilliseconds =
            std::max(summary.MaxTotalLodMilliseconds, stats.RoamTotalMilliseconds);
        summary.MaxGpuPassSumMilliseconds =
            std::max(summary.MaxGpuPassSumMilliseconds, stats.RoamGpuPassSumMilliseconds);
        summary.MaxRenderMilliseconds =
            std::max(summary.MaxRenderMilliseconds, stats.RoamRenderMilliseconds);
        summary.MaxFrameFenceWaitMilliseconds =
            std::max(summary.MaxFrameFenceWaitMilliseconds, stats.RoamFrameFenceWaitMilliseconds);
        summary.MaxTriangles = std::max(summary.MaxTriangles, stats.TriangleCount);
        summary.MaxNodes = std::max(summary.MaxNodes, stats.RoamNodeCount);
        // CPU 占用和 worker 数一起观察并行路径是否真正生效
        summary.MaxCpuUtilizationPercent =
            std::max(summary.MaxCpuUtilizationPercent, stats.RoamCpuUtilizationPercent);
        summary.MaxCpuWorkerCount = std::max(summary.MaxCpuWorkerCount, stats.RoamCpuWorkerCount);
        summary.MaxCpuGpuUploadBytes = std::max(summary.MaxCpuGpuUploadBytes, stats.RoamCpuGpuUploadBytes);
        summary.MaxCpuGpuReadbackBytes = std::max(summary.MaxCpuGpuReadbackBytes, stats.RoamCpuGpuReadbackBytes);
        summary.MaxDepthSetting = std::max(summary.MaxDepthSetting, stats.RoamMaxDepthSetting);
        summary.MaxDepthReached = std::max(summary.MaxDepthReached, stats.RoamMaxDepthReached);
        summary.MaxInvalidTopologyCount = std::max(summary.MaxInvalidTopologyCount, invalidTopologyCount);
    }

    const double sampleCount = static_cast<double>(summary.SampleCount);
    summary.AverageFrameMilliseconds = static_cast<float>(totalFrameMilliseconds / sampleCount);
    summary.AverageTotalLodMilliseconds = static_cast<float>(totalLodMilliseconds / sampleCount);
    summary.AverageCpuUpdateMilliseconds = static_cast<float>(totalCpuUpdateMilliseconds / sampleCount);
    summary.AverageCpuPrepareMilliseconds = static_cast<float>(totalCpuPrepareMilliseconds / sampleCount);
    summary.AverageCpuMergeCandidateMarkMilliseconds =
        static_cast<float>(totalCpuMergeCandidateMarkMilliseconds / sampleCount);
    summary.AverageCpuMergeTopologyMilliseconds =
        static_cast<float>(totalCpuMergeTopologyMilliseconds / sampleCount);
    summary.AverageCpuBudgetLeafCollectMilliseconds =
        static_cast<float>(totalCpuBudgetLeafCollectMilliseconds / sampleCount);
    summary.AverageCpuErrorEvalMilliseconds = static_cast<float>(totalCpuErrorEvalMilliseconds / sampleCount);
    summary.AverageCpuSplitCandidateMarkMilliseconds =
        static_cast<float>(totalCpuSplitCandidateMarkMilliseconds / sampleCount);
    summary.AverageCpuSplitTopologyMilliseconds =
        static_cast<float>(totalCpuSplitTopologyMilliseconds / sampleCount);
    summary.AverageCpuFinalLeafCollectMilliseconds =
        static_cast<float>(totalCpuFinalLeafCollectMilliseconds / sampleCount);
    summary.AverageCpuMeshEmitMilliseconds = static_cast<float>(totalCpuMeshEmitMilliseconds / sampleCount);
    summary.AverageCpuFinalizeMilliseconds = static_cast<float>(totalCpuFinalizeMilliseconds / sampleCount);
    summary.AverageCpuUploadMilliseconds = static_cast<float>(totalCpuUploadMilliseconds / sampleCount);
    summary.AverageSplitMilliseconds = static_cast<float>(totalSplitMilliseconds / sampleCount);
    summary.AverageMergeMilliseconds = static_cast<float>(totalMergeMilliseconds / sampleCount);
    summary.AverageEmitMilliseconds = static_cast<float>(totalEmitMilliseconds / sampleCount);
    summary.AverageValidateMilliseconds = static_cast<float>(totalValidateMilliseconds / sampleCount);
    summary.AverageGpuInitialLeafCompactionMilliseconds =
        static_cast<float>(totalGpuInitialLeafCompactionMilliseconds / sampleCount);
    summary.AverageGpuErrorEvaluationMilliseconds =
        static_cast<float>(totalGpuErrorEvaluationMilliseconds / sampleCount);
    summary.AverageGpuSplitCandidateMarkingMilliseconds =
        static_cast<float>(totalGpuSplitCandidateMarkingMilliseconds / sampleCount);
    summary.AverageGpuMergeCandidateMarkingMilliseconds =
        static_cast<float>(totalGpuMergeCandidateMarkingMilliseconds / sampleCount);
    summary.AverageGpuSplitTopologyMilliseconds =
        static_cast<float>(totalGpuSplitTopologyMilliseconds / sampleCount);
    summary.AverageGpuActiveLeafResetMilliseconds =
        static_cast<float>(totalGpuActiveLeafResetMilliseconds / sampleCount);
    summary.AverageGpuFinalLeafCompactionMilliseconds =
        static_cast<float>(totalGpuFinalLeafCompactionMilliseconds / sampleCount);
    summary.AverageGpuMeshEmitMilliseconds =
        static_cast<float>(totalGpuMeshEmitMilliseconds / sampleCount);
    summary.AverageGpuPassSumMilliseconds = static_cast<float>(totalGpuPassSumMilliseconds / sampleCount);
    summary.AverageGpuSnapshotBuildMilliseconds =
        static_cast<float>(totalGpuSnapshotBuildMilliseconds / sampleCount);
    summary.AverageGpuBufferAllocationMilliseconds =
        static_cast<float>(totalGpuBufferAllocationMilliseconds / sampleCount);
    summary.AverageGpuDispatchWallMilliseconds =
        static_cast<float>(totalGpuDispatchWallMilliseconds / sampleCount);
    summary.AverageGpuQueryWaitMilliseconds =
        static_cast<float>(totalGpuQueryWaitMilliseconds / sampleCount);
    summary.AverageGpuReadbackWaitMilliseconds =
        static_cast<float>(totalGpuReadbackWaitMilliseconds / sampleCount);
    summary.AverageFrameFenceWaitMilliseconds =
        static_cast<float>(totalFrameFenceWaitMilliseconds / sampleCount);
    summary.AverageRenderMilliseconds = static_cast<float>(totalRenderMilliseconds / sampleCount);
    summary.AverageTriangles = totalTriangles / sampleCount;
    summary.AverageNodes = totalNodes / sampleCount;
    summary.AverageCpuUtilizationPercent = static_cast<float>(totalCpuUtilization / sampleCount);
    return summary;
}

RuntimeBenchmarkSummary SummaryForAlgorithm(
    const std::vector<RuntimeBenchmarkAlgorithmResult>& results,
    Algorithms::TerrainLodAlgorithmId algorithmId)
{
    const auto result = std::find_if(
        results.begin(),
        results.end(),
        [algorithmId](const RuntimeBenchmarkAlgorithmResult& candidate)
        {
            return candidate.AlgorithmId == algorithmId;
        });
    return result == results.end() ? RuntimeBenchmarkSummary{} : SummarizeRuntimeBenchmark(*result);
}

void WriteDetailedCsv(
    const std::filesystem::path& csvPath,
    const std::vector<RuntimeBenchmarkAlgorithmResult>& results)
{
    // CSV 保存逐帧明细，后续可直接导入表格或脚本做曲线
    std::ofstream csv{csvPath};
    if (!csv)
    {
        throw std::runtime_error{"Failed to create runtime benchmark CSV: " + csvPath.string()};
    }

    // 配置字段放在时间序列前，方便按高度图和参数筛选
    csv << "algorithm,buildConfiguration,graphicsBackend,graphicsAdapter,graphicsVersion,vSyncEnabled,"
        << "heightMapPath,heightMapWidth,heightMapHeight,terrainSize,heightScale,"
        << "maxDepthSetting,screenSpaceSplitThresholdPixels,"
        << "screenSpaceMergeThresholdPixels,triangleBudget,"
        << "timeSeconds,cameraX,cameraY,cameraZ,frameMilliseconds,triangles,nodes,"
        << "activeSplits,splits,forcedSplits,merges,candidatePeak,budgetRejectedSplits,tjunctions,invalidNeighbors,"
        << "invalidTopology,cpuWorkers,cpuUtilizationPercent,lodTotalMilliseconds,"
        << "cpuUpdateMilliseconds,cpuPrepareMilliseconds,cpuMergeCandidateMarkMilliseconds,"
        << "cpuMergeTopologyMilliseconds,cpuBudgetLeafCollectMilliseconds,cpuErrorEvalMilliseconds,"
        << "cpuSplitCandidateMarkMilliseconds,cpuSplitTopologyMilliseconds,"
        << "cpuFinalLeafCollectMilliseconds,cpuMeshEmitMilliseconds,cpuFinalizeMilliseconds,"
        << "cpuUploadMilliseconds,"
        << "gpuInitialLeafCompactionMilliseconds,gpuErrorEvaluationMilliseconds,"
        << "gpuSplitCandidateMarkingMilliseconds,gpuMergeCandidateMarkingMilliseconds,"
        << "gpuSplitTopologyMilliseconds,"
        << "gpuActiveLeafResetMilliseconds,gpuFinalLeafCompactionMilliseconds,"
        << "gpuMeshEmitMilliseconds,gpuPassSumMilliseconds,"
        << "gpuSnapshotBuildMilliseconds,gpuBufferAllocationMilliseconds,"
        << "gpuDispatchWallMilliseconds,gpuQueryWaitMilliseconds,gpuReadbackWaitMilliseconds,"
        << "frameFenceWaitMilliseconds,renderMilliseconds,"
        << "cpuGpuUploadBytes,cpuGpuReadbackBytes,splitMilliseconds,"
        << "mergeMilliseconds,emitMilliseconds,validateMilliseconds,maxDepthReached\n";

    csv << std::fixed << std::setprecision(3);
    for (const RuntimeBenchmarkAlgorithmResult& result : results)
    {
        // 算法名逐行写入，便于把多个算法拼在同一个 CSV 中筛选
        for (const RuntimeBenchmarkSample& sample : result.Samples)
        {
            const Render::TerrainRenderStats& stats = sample.Stats;
            csv << result.AlgorithmName << ','
                << sample.BuildConfiguration << ','
                << sample.GraphicsBackend << ','
                << sample.GraphicsAdapter << ','
                << sample.GraphicsVersion << ','
                << (sample.VSyncEnabled ? "true" : "false") << ','
                << stats.HeightMapPath.generic_string() << ','
                << stats.HeightMapWidth << ','
                << stats.HeightMapHeight << ','
                << stats.TerrainSize << ','
                << stats.HeightScale << ','
                << stats.RoamMaxDepthSetting << ','
                << stats.RoamScreenSpaceSplitThresholdPixels << ','
                << stats.RoamScreenSpaceMergeThresholdPixels << ','
                << stats.RoamTriangleBudgetSetting << ','
                << sample.TimeSeconds << ','
                << sample.CameraPosition.x << ','
                << sample.CameraPosition.y << ','
                << sample.CameraPosition.z << ','
                << sample.FrameMilliseconds << ','
                << stats.TriangleCount << ','
                << stats.RoamNodeCount << ','
                << stats.RoamActiveSplitCount << ','
                << stats.RoamSplitCount << ','
                << stats.RoamForcedSplitCount << ','
                << stats.RoamMergeCount << ','
                << stats.RoamCandidatePeakCount << ','
                << stats.RoamBudgetRejectedSplitCount << ','
                << stats.RoamTjunctionCount << ','
                << stats.RoamInvalidNeighborCount << ','
                << stats.RoamInvalidTopologyCount << ','
                << stats.RoamCpuWorkerCount << ','
                << stats.RoamCpuUtilizationPercent << ','
                << stats.RoamTotalMilliseconds << ','
                << stats.RoamUpdateMilliseconds << ','
                << stats.RoamCpuPrepareMilliseconds << ','
                << stats.RoamCpuMergeCandidateMarkMilliseconds << ','
                << stats.RoamCpuMergeTopologyMilliseconds << ','
                << stats.RoamCpuBudgetLeafCollectMilliseconds << ','
                << stats.RoamCpuErrorEvalMilliseconds << ','
                << stats.RoamCpuSplitCandidateMarkMilliseconds << ','
                << stats.RoamCpuSplitTopologyMilliseconds << ','
                << stats.RoamCpuFinalLeafCollectMilliseconds << ','
                << stats.RoamCpuMeshEmitMilliseconds << ','
                << stats.RoamCpuFinalizeMilliseconds << ','
                << stats.RoamCpuUploadMilliseconds << ','
                << stats.RoamGpuInitialLeafCompactionMilliseconds << ','
                << stats.RoamGpuErrorEvaluationMilliseconds << ','
                << stats.RoamGpuSplitCandidateMarkingMilliseconds << ','
                << stats.RoamGpuMergeCandidateMarkingMilliseconds << ','
                << stats.RoamGpuSplitTopologyMilliseconds << ','
                << stats.RoamGpuActiveLeafResetMilliseconds << ','
                << stats.RoamGpuFinalLeafCompactionMilliseconds << ','
                << stats.RoamGpuMeshEmitMilliseconds << ','
                << stats.RoamGpuPassSumMilliseconds << ','
                << stats.RoamGpuSnapshotBuildMilliseconds << ','
                << stats.RoamGpuBufferAllocationMilliseconds << ','
                << stats.RoamGpuDispatchWallMilliseconds << ','
                << stats.RoamGpuQueryWaitMilliseconds << ','
                << stats.RoamGpuReadbackWaitMilliseconds << ','
                << stats.RoamFrameFenceWaitMilliseconds << ','
                << stats.RoamRenderMilliseconds << ','
                << stats.RoamCpuGpuUploadBytes << ','
                << stats.RoamCpuGpuReadbackBytes << ','
                << stats.RoamSplitMilliseconds << ','
                << stats.RoamMergeMilliseconds << ','
                << stats.RoamEmitMilliseconds << ','
                << stats.RoamValidateMilliseconds << ','
                << stats.RoamMaxDepthReached << '\n';
        }
    }
}

void WriteSummaryMarkdown(
    const std::filesystem::path& markdownPath,
    const std::filesystem::path& csvPath,
    const std::vector<RuntimeBenchmarkAlgorithmResult>& results,
    const std::vector<std::string>& notes)
{
    // Markdown 是面向人看的主输出，字段数量控制在一屏内
    std::ofstream markdown{markdownPath};
    if (!markdown)
    {
        throw std::runtime_error{"Failed to create runtime benchmark table: " + markdownPath.string()};
    }

    markdown << "# 运行时基准测试报告\n\n";
    float sampledDurationSeconds = 0.0F;
    for (const RuntimeBenchmarkAlgorithmResult& result : results)
    {
        if (!result.Samples.empty())
        {
            sampledDurationSeconds = std::max(sampledDurationSeconds, result.Samples.back().TimeSeconds);
        }
    }

    markdown << "- 相机路径：从地形边缘中点移动到地形中心\n";
    markdown << "- 每种算法的采样时长：" << sampledDurationSeconds << " 秒\n";
    markdown << "- 详细 CSV：`" << csvPath.filename().string() << "`\n\n";
    for (const std::string& note : notes)
    {
        markdown << "- " << note << "\n";
    }
    if (!notes.empty())
    {
        markdown << '\n';
    }

    if (const Render::TerrainRenderStats* stats = FindFirstSampleStats(results))
    {
        // 顶部配置块解释 UI 设置和实际达到深度的差异
        markdown << "- Height map：`" << stats->HeightMapPath.generic_string() << "` "
                 << stats->HeightMapWidth << "x" << stats->HeightMapHeight << "\n";
        markdown << "- Terrain size：" << stats->TerrainSize << "\n";
        markdown << "- Height scale：" << stats->HeightScale << "\n";
        markdown << "- Max depth 设置：" << stats->RoamMaxDepthSetting << "\n";
        markdown << "- ROAM 屏幕空间 split/merge 阈值："
                 << stats->RoamScreenSpaceSplitThresholdPixels << " px / "
                 << stats->RoamScreenSpaceMergeThresholdPixels << " px\n";
        markdown << "- ROAM triangle budget：" << stats->RoamTriangleBudgetSetting << "\n\n";
    }
    markdown << "## 总体结果\n\n";
    markdown << "| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | "
             << "Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | "
             << "Max Workers | Config Max Depth | Reached Max Depth | Max Topology Issues |\n";
    markdown << "| ---";
    for (int column = 0; column < 15; ++column)
    {
        markdown << " | ---:";
    }
    markdown << " |\n";
    markdown << std::fixed << std::setprecision(2);

    for (const RuntimeBenchmarkAlgorithmResult& result : results)
    {
        // 每个算法一行，和 UI 顺序保持一致
        const RuntimeBenchmarkSummary summary = SummarizeRuntimeBenchmark(result);
        markdown << "| " << result.AlgorithmName
                 << " | " << summary.SampleCount
                 << " | " << summary.AverageFrameMilliseconds
                 << " | " << summary.MaxFrameMilliseconds
                 << " | " << summary.AverageTotalLodMilliseconds
                 << " | " << summary.MaxTotalLodMilliseconds
                 << " | " << summary.AverageTriangles
                 << " | " << summary.MaxTriangles
                 << " | " << summary.AverageNodes
                 << " | " << summary.MaxNodes
                 << " | " << summary.AverageCpuUtilizationPercent
                 << " | " << summary.MaxCpuUtilizationPercent
                 << " | " << summary.MaxCpuWorkerCount
                 << " | " << summary.MaxDepthSetting
                 << " | " << summary.MaxDepthReached
                 << " | " << summary.MaxInvalidTopologyCount
                 << " |\n";
    }

    const RuntimeBenchmarkSummary classicSummary =
        SummaryForAlgorithm(results, Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam);
    const RuntimeBenchmarkSummary dodSummary =
        SummaryForAlgorithm(results, Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam);
    const RuntimeBenchmarkSummary gpuSummary =
        SummaryForAlgorithm(results, Algorithms::TerrainLodAlgorithmId::GpuRoamLike);

    markdown << "\n## ROAM 逻辑阶段对比\n\n";
    markdown << "表中数值均为平均毫秒数。GPU ROAM-like 是混合路径：先运行完整的 CPU DOD "
             << "拓扑基线，再追加一轮 GPU split-only 细分和 GPU mesh emit。两个 GPU-like 列分别展示 "
             << "CPU 与 shader 的重复职责，避免把 shader 链误解为完整的 GPU ROAM 实现。\n\n";
    markdown << "| ROAM 逻辑阶段 | Classic CPU | DOD CPU | GPU-like CPU baseline | "
             << "GPU-like shader | 阶段映射与限制 |\n";
    markdown << "| --- | ---: | ---: | ---: | ---: | --- |\n";
    markdown << std::fixed << std::setprecision(4);
    markdown << "| Prepare / 帧状态 | " << classicSummary.AverageCpuPrepareMilliseconds
             << " | " << dodSummary.AverageCpuPrepareMilliseconds
             << " | " << gpuSummary.AverageCpuPrepareMilliseconds
             << " | N/A | CPU 准备持久拓扑和 snapshot 输入 |\n";
    markdown << "| Merge 候选评分 | " << classicSummary.AverageCpuMergeCandidateMarkMilliseconds
             << " | " << dodSummary.AverageCpuMergeCandidateMarkMilliseconds
             << " | " << gpuSummary.AverageCpuMergeCandidateMarkMilliseconds
             << " | " << gpuSummary.AverageGpuMergeCandidateMarkingMilliseconds
             << " | Shader 为诊断重新扫描已 split 的父节点；GPU 不提交这些候选 |\n";
    markdown << "| Merge 拓扑提交 / 向上级联 | " << classicSummary.AverageCpuMergeTopologyMilliseconds
             << " | " << dodSummary.AverageCpuMergeTopologyMilliseconds
             << " | " << gpuSummary.AverageCpuMergeTopologyMilliseconds
             << " | N/A | 持久 merge、邻接修复和级联回收只由 CPU DOD 执行 |\n";
    markdown << "| Split 前 active leaf 收集 | " << classicSummary.AverageCpuBudgetLeafCollectMilliseconds
             << " | " << dodSummary.AverageCpuBudgetLeafCollectMilliseconds
             << " | " << gpuSummary.AverageCpuBudgetLeafCollectMilliseconds
             << " | " << gpuSummary.AverageGpuInitialLeafCompactionMilliseconds
             << " | GPU 在评分前再次压缩已上传的 CPU 拓扑 |\n";
    markdown << "| 视点相关 leaf error / 视锥测试 | " << classicSummary.AverageCpuErrorEvalMilliseconds
             << " | " << dodSummary.AverageCpuErrorEvalMilliseconds
             << " | " << gpuSummary.AverageCpuErrorEvalMilliseconds
             << " | " << gpuSummary.AverageGpuErrorEvaluationMilliseconds
             << " | Classic 在 split 扫描中内联评估；GPU 基于 snapshot 重复执行 DOD 评分 |\n";
    markdown << "| Split 候选标记 | " << classicSummary.AverageCpuSplitCandidateMarkMilliseconds
             << " | " << dodSummary.AverageCpuSplitCandidateMarkMilliseconds
             << " | " << gpuSummary.AverageCpuSplitCandidateMarkMilliseconds
             << " | " << gpuSummary.AverageGpuSplitCandidateMarkingMilliseconds
             << " | 按 threshold 和 max depth 分类；GPU append 顺序不是误差优先级顺序 |\n";
    markdown << "| Split 拓扑 / 裂缝约束提交 | " << classicSummary.AverageCpuSplitTopologyMilliseconds
             << " | " << dodSummary.AverageCpuSplitTopologyMilliseconds
             << " | " << gpuSummary.AverageCpuSplitTopologyMilliseconds
             << " | " << gpuSummary.AverageGpuSplitTopologyMilliseconds
             << " | GPU 只提交一轮受预算约束的细分，并只处理直接 base-neighbor diamond pair；没有递归 forced-split 链 |\n";
    markdown << "| 细分后 active leaf 收集 | " << classicSummary.AverageCpuFinalLeafCollectMilliseconds
             << " | " << dodSummary.AverageCpuFinalLeafCollectMilliseconds
             << " | " << gpuSummary.AverageCpuFinalLeafCollectMilliseconds
             << " | " << (gpuSummary.AverageGpuActiveLeafResetMilliseconds +
                              gpuSummary.AverageGpuFinalLeafCompactionMilliseconds)
             << " | GPU 数值包含 counter reset 和 split 后 leaf compaction |\n";
    markdown << "| Mesh emit / draw argument 生成 | " << classicSummary.AverageCpuMeshEmitMilliseconds
             << " | " << dodSummary.AverageCpuMeshEmitMilliseconds
             << " | " << gpuSummary.AverageCpuMeshEmitMilliseconds
             << " | " << gpuSummary.AverageGpuMeshEmitMilliseconds
             << " | GPU 输出非共享顶点、索引和 indirect draw argument |\n";
    markdown << "| Finalize / 发布 packet | " << classicSummary.AverageCpuFinalizeMilliseconds
             << " | " << dodSummary.AverageCpuFinalizeMilliseconds
             << " | " << gpuSummary.AverageCpuFinalizeMilliseconds
             << " | N/A | CPU 发布统计和 renderer 资源契约 |\n";

    markdown << "\n## CPU 实现阶段\n\n";
    markdown << "`CPU update` 包含下表中互斥的算法阶段；`CPU upload` 是算法返回后的 renderer 上传。"
             << "Classic 在扫描和弹出 split queue 时评估屏幕误差，因此单独的 `Error eval` 为零，"
             << "这部分工作仍计入 `Split mark` / `Split topology`。\n\n";
    markdown << "| Algorithm | CPU update | Prepare | Merge mark | Merge topology | Budget leaf collect | "
             << "Error eval | Split mark | Split topology | Final leaf collect | Mesh emit | "
             << "Finalize | CPU upload |\n";
    markdown << "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n";
    for (const RuntimeBenchmarkAlgorithmResult& result : results)
    {
        const RuntimeBenchmarkSummary summary = SummarizeRuntimeBenchmark(result);
        markdown << "| " << result.AlgorithmName
                 << " | " << summary.AverageCpuUpdateMilliseconds
                 << " | " << summary.AverageCpuPrepareMilliseconds
                 << " | " << summary.AverageCpuMergeCandidateMarkMilliseconds
                 << " | " << summary.AverageCpuMergeTopologyMilliseconds
                 << " | " << summary.AverageCpuBudgetLeafCollectMilliseconds
                 << " | " << summary.AverageCpuErrorEvalMilliseconds
                 << " | " << summary.AverageCpuSplitCandidateMarkMilliseconds
                 << " | " << summary.AverageCpuSplitTopologyMilliseconds
                 << " | " << summary.AverageCpuFinalLeafCollectMilliseconds
                 << " | " << summary.AverageCpuMeshEmitMilliseconds
                 << " | " << summary.AverageCpuFinalizeMilliseconds
                 << " | " << summary.AverageCpuUploadMilliseconds << " |\n";
    }

    markdown << "\n### 原生 pass 包络\n\n";
    markdown << "这些是各实现原有的外围 pass 计时。它们与上方互斥阶段重叠，不能重复相加。\n\n";
    markdown << "| Algorithm | Split | Merge | Emit | Validate |\n";
    markdown << "| --- | ---: | ---: | ---: | ---: |\n";
    for (const RuntimeBenchmarkAlgorithmResult& result : results)
    {
        const RuntimeBenchmarkSummary summary = SummarizeRuntimeBenchmark(result);
        markdown << "| " << result.AlgorithmName
                 << " | " << summary.AverageSplitMilliseconds
                 << " | " << summary.AverageMergeMilliseconds
                 << " | " << summary.AverageEmitMilliseconds
                 << " | " << summary.AverageValidateMilliseconds << " |\n";
    }

    markdown << "\n## GPU shader dispatch 明细\n\n";
    markdown << "每个数值都是一个物理 dispatch 的延迟 GPU timestamp 结果。`Pass sum` 是八个互不重叠区间之和。"
             << "本表用于解释上方 GPU-like shader 列，不能据此认定所有 ROAM 阶段都已在 GPU 实现。"
             << "`Dispatch wall`、query wait、readback wait 和 frame-fence wait 都是 CPU 侧开销，"
             << "不计入 `Pass sum`。\n\n";
    markdown << "| Algorithm | Pre-split leaf collect | Leaf error / frustum | Split candidate mark | "
             << "Merge candidate score | Split / direct-diamond commit | Leaf-counter reset | "
             << "Post-refine leaf collect | Mesh emit / draw args | Pass sum | Max pass sum |\n";
    markdown << "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n";
    markdown << std::setprecision(4);
    for (const RuntimeBenchmarkAlgorithmResult& result : results)
    {
        if (result.AlgorithmId != Algorithms::TerrainLodAlgorithmId::GpuRoamLike)
        {
            continue;
        }
        const RuntimeBenchmarkSummary summary = SummarizeRuntimeBenchmark(result);
        markdown << "| " << result.AlgorithmName
                 << " | " << summary.AverageGpuInitialLeafCompactionMilliseconds
                 << " | " << summary.AverageGpuErrorEvaluationMilliseconds
                 << " | " << summary.AverageGpuSplitCandidateMarkingMilliseconds
                 << " | " << summary.AverageGpuMergeCandidateMarkingMilliseconds
                 << " | " << summary.AverageGpuSplitTopologyMilliseconds
                 << " | " << summary.AverageGpuActiveLeafResetMilliseconds
                 << " | " << summary.AverageGpuFinalLeafCompactionMilliseconds
                 << " | " << summary.AverageGpuMeshEmitMilliseconds
                 << " | " << summary.AverageGpuPassSumMilliseconds
                 << " | " << summary.MaxGpuPassSumMilliseconds << " |\n";
    }

    markdown << "\n## GPU 编排与渲染\n\n";
    markdown << std::setprecision(2);
    markdown << "| Algorithm | Snapshot build | Buffer allocation | Dispatch wall | Query wait | "
             << "Readback wait | Frame fence wait | Render | Max render | Max upload B | Max readback B |\n";
    markdown << "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n";
    for (const RuntimeBenchmarkAlgorithmResult& result : results)
    {
        const RuntimeBenchmarkSummary summary = SummarizeRuntimeBenchmark(result);
        markdown << "| " << result.AlgorithmName
                 << " | " << summary.AverageGpuSnapshotBuildMilliseconds
                 << " | " << summary.AverageGpuBufferAllocationMilliseconds
                 << " | " << summary.AverageGpuDispatchWallMilliseconds
                 << " | " << summary.AverageGpuQueryWaitMilliseconds
                 << " | " << summary.AverageGpuReadbackWaitMilliseconds
                 << " | " << summary.AverageFrameFenceWaitMilliseconds
                 << " | " << summary.AverageRenderMilliseconds
                 << " | " << summary.MaxRenderMilliseconds
                 << " | " << summary.MaxCpuGpuUploadBytes
                 << " | " << summary.MaxCpuGpuReadbackBytes << " |\n";
    }
}
} // namespace

std::string RuntimeBenchmarkAlgorithmDisplayName(Algorithms::TerrainLodAlgorithmId algorithmId)
{
    switch (algorithmId)
    {
    case Algorithms::TerrainLodAlgorithmId::ClassicCpuRoam:
        return "Classic CPU ROAM";
    case Algorithms::TerrainLodAlgorithmId::DataOrientedCpuRoam:
        return "Data-Oriented CPU ROAM";
    case Algorithms::TerrainLodAlgorithmId::GpuRoamLike:
        return "GPU ROAM-like";
    case Algorithms::TerrainLodAlgorithmId::Cbt2024:
        return "CBT 2024（程序化绘制验证）";
    case Algorithms::TerrainLodAlgorithmId::Count:
        break;
    }

    return "Unknown ROAM";
}

RuntimeBenchmarkReportPaths WriteRuntimeBenchmarkReport(
    const std::vector<RuntimeBenchmarkAlgorithmResult>& results,
    const std::vector<std::string>& notes)
{
    // benchmark-output 已被 gitignore 忽略，生成报告不会污染提交
    const std::filesystem::path outputDirectory{"benchmark-output"};
    std::filesystem::create_directories(outputDirectory);

    const std::string timestamp = MakeReportTimestamp();
    RuntimeBenchmarkReportPaths paths{};
    // 同一时间戳绑定 Markdown 和 CSV，用户可以互相追溯
    paths.MarkdownPath = outputDirectory / ("runtime-benchmark-" + timestamp + ".md");
    paths.CsvPath = outputDirectory / ("runtime-benchmark-" + timestamp + ".csv");

    // 先写 CSV 再写 Markdown，汇总表可以引用已确定的明细文件名
    WriteDetailedCsv(paths.CsvPath, results);
    WriteSummaryMarkdown(paths.MarkdownPath, paths.CsvPath, results, notes);
    // 返回两个路径，让 Application 同时输出日志和刷新 UI
    return paths;
}
} // namespace ParallelRoam::App
