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
    float AverageCpuUploadMilliseconds{0.0F};
    float AverageGpuComputeMilliseconds{0.0F};
    float MaxGpuComputeMilliseconds{0.0F};
    float AverageGpuSnapshotBuildMilliseconds{0.0F};
    float AverageGpuBufferAllocationMilliseconds{0.0F};
    float AverageGpuDispatchWallMilliseconds{0.0F};
    float AverageGpuQueryWaitMilliseconds{0.0F};
    float AverageGpuReadbackWaitMilliseconds{0.0F};

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
    double totalCpuUploadMilliseconds = 0.0;
    double totalGpuComputeMilliseconds = 0.0;
    double totalGpuSnapshotBuildMilliseconds = 0.0;
    double totalGpuBufferAllocationMilliseconds = 0.0;
    double totalGpuDispatchWallMilliseconds = 0.0;
    double totalGpuQueryWaitMilliseconds = 0.0;
    double totalGpuReadbackWaitMilliseconds = 0.0;
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
        totalCpuUploadMilliseconds += stats.RoamCpuUploadMilliseconds;
        totalGpuComputeMilliseconds += stats.RoamGpuComputeMilliseconds;
        totalGpuSnapshotBuildMilliseconds += stats.RoamGpuSnapshotBuildMilliseconds;
        totalGpuBufferAllocationMilliseconds += stats.RoamGpuBufferAllocationMilliseconds;
        totalGpuDispatchWallMilliseconds += stats.RoamGpuDispatchWallMilliseconds;
        totalGpuQueryWaitMilliseconds += stats.RoamGpuQueryWaitMilliseconds;
        totalGpuReadbackWaitMilliseconds += stats.RoamGpuReadbackWaitMilliseconds;
        totalTriangles += static_cast<double>(stats.TriangleCount);
        totalNodes += static_cast<double>(stats.RoamNodeCount);
        totalCpuUtilization += stats.RoamCpuUtilizationPercent;

        summary.MaxFrameMilliseconds = std::max(summary.MaxFrameMilliseconds, sample.FrameMilliseconds);
        summary.MaxTotalLodMilliseconds =
            std::max(summary.MaxTotalLodMilliseconds, stats.RoamTotalMilliseconds);
        summary.MaxGpuComputeMilliseconds =
            std::max(summary.MaxGpuComputeMilliseconds, stats.RoamGpuComputeMilliseconds);
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
    summary.AverageCpuUploadMilliseconds = static_cast<float>(totalCpuUploadMilliseconds / sampleCount);
    summary.AverageGpuComputeMilliseconds = static_cast<float>(totalGpuComputeMilliseconds / sampleCount);
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
    summary.AverageTriangles = totalTriangles / sampleCount;
    summary.AverageNodes = totalNodes / sampleCount;
    summary.AverageCpuUtilizationPercent = static_cast<float>(totalCpuUtilization / sampleCount);
    return summary;
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
    csv << "algorithm,buildConfiguration,vSyncEnabled,"
        << "heightMapPath,heightMapWidth,heightMapHeight,terrainSize,heightScale,"
        << "maxDepthSetting,splitThreshold,mergeThreshold,distanceScale,"
        << "timeSeconds,cameraX,cameraY,cameraZ,frameMilliseconds,triangles,nodes,"
        << "activeSplits,splits,forcedSplits,merges,candidatePeak,tjunctions,invalidNeighbors,"
        << "invalidTopology,cpuWorkers,cpuUtilizationPercent,lodTotalMilliseconds,"
        << "cpuUpdateMilliseconds,cpuUploadMilliseconds,gpuComputeMilliseconds,"
        << "gpuSnapshotBuildMilliseconds,gpuBufferAllocationMilliseconds,"
        << "gpuDispatchWallMilliseconds,gpuQueryWaitMilliseconds,gpuReadbackWaitMilliseconds,"
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
                << (sample.VSyncEnabled ? "true" : "false") << ','
                << stats.HeightMapPath.generic_string() << ','
                << stats.HeightMapWidth << ','
                << stats.HeightMapHeight << ','
                << stats.TerrainSize << ','
                << stats.HeightScale << ','
                << stats.RoamMaxDepthSetting << ','
                << stats.RoamSplitThreshold << ','
                << stats.RoamMergeThreshold << ','
                << stats.RoamDistanceScale << ','
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
                << stats.RoamTjunctionCount << ','
                << stats.RoamInvalidNeighborCount << ','
                << stats.RoamInvalidTopologyCount << ','
                << stats.RoamCpuWorkerCount << ','
                << stats.RoamCpuUtilizationPercent << ','
                << stats.RoamTotalMilliseconds << ','
                << stats.RoamUpdateMilliseconds << ','
                << stats.RoamCpuUploadMilliseconds << ','
                << stats.RoamGpuComputeMilliseconds << ','
                << stats.RoamGpuSnapshotBuildMilliseconds << ','
                << stats.RoamGpuBufferAllocationMilliseconds << ','
                << stats.RoamGpuDispatchWallMilliseconds << ','
                << stats.RoamGpuQueryWaitMilliseconds << ','
                << stats.RoamGpuReadbackWaitMilliseconds << ','
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

    markdown << "# Runtime Benchmark\n\n";
    markdown << "- Camera path: edge midpoint to terrain center\n";
    markdown << "- Duration per algorithm: 10 seconds\n";
    markdown << "- Detailed CSV: `" << csvPath.filename().string() << "`\n\n";
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
        markdown << "- Height map: `" << stats->HeightMapPath.generic_string() << "` "
                 << stats->HeightMapWidth << "x" << stats->HeightMapHeight << "\n";
        markdown << "- Terrain size: " << stats->TerrainSize << "\n";
        markdown << "- Height scale: " << stats->HeightScale << "\n";
        markdown << "- Max depth setting: " << stats->RoamMaxDepthSetting << "\n";
        markdown << "- Distance scale: " << stats->RoamDistanceScale << "\n";
        markdown << "- Split/Merge thresholds: "
                 << stats->RoamSplitThreshold << " / " << stats->RoamMergeThreshold << "\n\n";
    }
    markdown << "| Algorithm | Samples | Avg Frame ms | Max Frame ms | Avg LOD ms | Max LOD ms | "
             << "Avg CPU Update ms | Avg CPU Upload ms | Avg GPU ms | Max GPU ms | "
             << "Avg GPU Snapshot ms | Avg GPU Alloc ms | Avg GPU Dispatch Wall ms | "
             << "Avg GPU Query Wait ms | Avg GPU Readback Wait ms | "
             << "Avg Triangles | Max Triangles | Avg Nodes | Max Nodes | Avg CPU % | Max CPU % | "
             << "Max Workers | Max Upload B | Max Readback B | Config Max Depth | Reached Max Depth | Max Topology Issues |\n";
    markdown << "| ---";
    for (int column = 0; column < 26; ++column)
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
                 << " | " << summary.AverageCpuUpdateMilliseconds
                 << " | " << summary.AverageCpuUploadMilliseconds
                 << " | " << summary.AverageGpuComputeMilliseconds
                 << " | " << summary.MaxGpuComputeMilliseconds
                 << " | " << summary.AverageGpuSnapshotBuildMilliseconds
                 << " | " << summary.AverageGpuBufferAllocationMilliseconds
                 << " | " << summary.AverageGpuDispatchWallMilliseconds
                 << " | " << summary.AverageGpuQueryWaitMilliseconds
                 << " | " << summary.AverageGpuReadbackWaitMilliseconds
                 << " | " << summary.AverageTriangles
                 << " | " << summary.MaxTriangles
                 << " | " << summary.AverageNodes
                 << " | " << summary.MaxNodes
                 << " | " << summary.AverageCpuUtilizationPercent
                 << " | " << summary.MaxCpuUtilizationPercent
                 << " | " << summary.MaxCpuWorkerCount
                 << " | " << summary.MaxCpuGpuUploadBytes
                 << " | " << summary.MaxCpuGpuReadbackBytes
                 << " | " << summary.MaxDepthSetting
                 << " | " << summary.MaxDepthReached
                 << " | " << summary.MaxInvalidTopologyCount
                 << " |\n";
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
