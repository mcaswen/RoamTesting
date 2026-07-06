#include "benchmark/TerrainLodBenchmark.h"

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.h"
#include "algorithms/data_oriented_roam/DataOrientedRoamTerrainLodAlgorithm.h"
#include "algorithms/gpu_roam/GpuRoamTerrainLodAlgorithm.h"
#include "terrain/HeightMap.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ParallelRoam::Benchmark
{
namespace
{
using Clock = std::chrono::steady_clock;

// benchmark 的核心约束是三算法共享同一组输入
// 算法只通过 ITerrainLodAlgorithm 边界接入
struct BenchmarkCameraKeyframe
{
    std::string Name;
    glm::vec3 Position{0.0F};
    float TimeSeconds{0.0F};
};

// HeightMap、terrain size、LOD 阈值和相机路径都由这里固定
// 这样 Classic、DOD 和 GPU 输出统计才能横向比较
struct BenchmarkScenario
{
    std::string Name;
    std::filesystem::path HeightMapPath;
    Algorithms::TerrainLodSettings Settings;
    std::vector<BenchmarkCameraKeyframe> CameraPath;
    // Smoke 会打开拓扑验证并要求近处细节增长
    bool RequireTopologyClean{false};
    bool RequireNearDetailIncrease{false};
};

// smoke 偏回归测试，standard 偏性能样本
// 两者共用同一套 frame result 和 CSV 字段
struct BenchmarkFrameResult
{
    std::string AlgorithmName;
    std::string ProfileName;
    std::string CameraName;
    int FrameIndex{0};
    float TimeSeconds{0.0F};
    glm::vec3 CameraPosition{0.0F};
    int HeightMapWidth{0};
    int HeightMapHeight{0};
    std::size_t VertexCount{0};
    std::size_t IndexCount{0};
    std::size_t TriangleCount{0};
    Algorithms::TerrainLodStats Stats;
    // wall clock 用于发现接口外开销
    float BuildWallMilliseconds{0.0F};
    bool Passed{false};
};

// 新算法缺失时只在 all 模式下 skip
// 显式选择缺失算法必须失败以防漏跑
struct BenchmarkAlgorithmRun
{
    std::string AlgorithmName;
    std::string UnavailableReason;
    // Available 和 Passed 分开保存，all 模式不会把 skip 当作失败
    bool Available{false};
    bool Passed{false};
    std::vector<BenchmarkFrameResult> Frames;
};

std::string ToString(BenchmarkProfile profile)
{
    switch (profile)
    {
    case BenchmarkProfile::Smoke:
        return "smoke";
    case BenchmarkProfile::Standard:
        return "standard";
    }

    return "unknown";
}

std::string ToString(BenchmarkAlgorithmSelection selection)
{
    switch (selection)
    {
    case BenchmarkAlgorithmSelection::Classic:
        return "classic";
    case BenchmarkAlgorithmSelection::DataOriented:
        return "dod";
    case BenchmarkAlgorithmSelection::Gpu:
        return "gpu";
    case BenchmarkAlgorithmSelection::All:
        return "all";
    }

    return "unknown";
}

std::vector<BenchmarkCameraKeyframe> MakeStandardCameraPath()
{
    // Standard 使用 64 帧闭合 flyover
    // 轨迹覆盖远景、中心、侧向和高度变化
    // 目的是让 split 与 merge 都在同一次回放中出现
    std::vector<BenchmarkCameraKeyframe> path;
    path.reserve(64);

    constexpr int FrameCount = 64;
    constexpr float Radius = 22.0F;
    constexpr float Height = 7.5F;
    constexpr float DurationSeconds = 16.0F;

    for (int index = 0; index < FrameCount; ++index)
    {
        // wave 让路径不是纯圆
        // 这样同一距离下会经过不同局部高度变化区域
        const float t = static_cast<float>(index) / static_cast<float>(FrameCount - 1);
        const float angle = t * 6.28318530718F;
        const float wave = std::sin(t * 12.56637061436F) * 3.5F;
        BenchmarkCameraKeyframe frame{};
        frame.Name = "flyover-" + std::to_string(index);
        frame.Position = glm::vec3{
            std::cos(angle) * Radius,
            Height + std::sin(angle * 1.7F) * 2.0F,
            std::sin(angle) * Radius + wave,
        };
        frame.TimeSeconds = t * DurationSeconds;
        path.push_back(frame);
    }

    return path;
}

BenchmarkScenario MakeScenario(BenchmarkProfile profile)
{
    BenchmarkScenario scenario{};
    // 这些参数是三版本对比的控制变量
    // 不在算法内部各自决定
    // 否则 CSV 里的时间和三角形数没有公平比较意义
    scenario.Name = ToString(profile);
    scenario.Settings.TerrainSize = 30.0F;
    scenario.Settings.HeightScale = 4.0F;
    scenario.Settings.MaxDepth = 14;
    scenario.Settings.SplitThreshold = 0.04F;
    scenario.Settings.MergeThreshold = 0.02F;
    scenario.Settings.DistanceScale = 24.0F;
    scenario.Settings.EnableLocalConstraints = true;

    if (profile == BenchmarkProfile::Smoke)
    {
        // Smoke 使用小高度图和 4 个代表性视点
        // 拓扑验证开启
        // 适合提交前快速发现裂缝和近细远粗退化
        scenario.HeightMapPath = "assets/heightmaps/Hm_Terrain_Test_129.pgm";
        scenario.Settings.EnableTopologyValidation = true;
        scenario.RequireTopologyClean = true;
        scenario.RequireNearDetailIncrease = true;
        scenario.CameraPath = {
            // far 建立远处低细节基线
            BenchmarkCameraKeyframe{"far", glm::vec3{0.0F, 14.0F, 28.0F}, 0.0F},
            // center 要显著增加 active triangles
            BenchmarkCameraKeyframe{"center", glm::vec3{0.0F, 4.0F, 0.0F}, 1.0F},
            // near-corner 检查非中心区域也能触发局部细分
            BenchmarkCameraKeyframe{"near-corner", glm::vec3{-13.0F, 2.5F, -13.0F}, 2.0F},
            // center-return 检查持久拓扑和 merge 后的再次细分稳定性
            BenchmarkCameraKeyframe{"center-return", glm::vec3{0.0F, 4.0F, 0.0F}, 3.0F},
        };
        return scenario;
    }

    // Standard 使用更大 HeightMap 和较长路径
    // 不要求每帧拓扑验证
    // 重点是记录稳定的各 pass 耗时分布
    scenario.HeightMapPath = "assets/heightmaps/Hm_Terrain_Peking_513.png";
    scenario.Settings.EnableTopologyValidation = false;
    scenario.RequireTopologyClean = false;
    scenario.RequireNearDetailIncrease = false;
    scenario.CameraPath = MakeStandardCameraPath();
    return scenario;
}

std::unique_ptr<Algorithms::ITerrainLodAlgorithm> CreateAlgorithm(BenchmarkAlgorithmSelection selection)
{
    // benchmark factory 是算法可用性的单一入口
    // 新算法接入后必须在这里注册
    // renderer 侧不需要知道 benchmark 的选择枚举
    if (selection == BenchmarkAlgorithmSelection::Classic)
    {
        return std::make_unique<Algorithms::ClassicRoam::ClassicRoamTerrainLodAlgorithm>();
    }

    if (selection == BenchmarkAlgorithmSelection::DataOriented)
    {
        return std::make_unique<Algorithms::DataOrientedRoam::DataOrientedRoamTerrainLodAlgorithm>();
    }

    if (selection == BenchmarkAlgorithmSelection::Gpu)
    {
        return std::make_unique<Algorithms::GpuRoam::GpuRoamTerrainLodAlgorithm>();
    }

    return nullptr;
}

std::vector<BenchmarkAlgorithmSelection> ExpandAlgorithmSelection(BenchmarkAlgorithmSelection selection)
{
    if (selection != BenchmarkAlgorithmSelection::All)
    {
        return {selection};
    }

    // all 的顺序固定为 Classic、DOD、GPU
    // 输出和 CSV 都能保持稳定列对比
    return {
        BenchmarkAlgorithmSelection::Classic,
        BenchmarkAlgorithmSelection::DataOriented,
        BenchmarkAlgorithmSelection::Gpu,
    };
}

bool HasInvalidTopology(const Algorithms::TerrainLodStats& stats)
{
    // 三类拓扑错误都属于 smoke profile 的硬失败
    // Standard profile 可选择关闭 validator 只采集性能
    return stats.TjunctionCount != 0U ||
           stats.InvalidNeighborCount != 0U ||
           stats.InvalidTopologyCount != 0U;
}

bool ValidateFrame(
    const BenchmarkScenario& scenario,
    const Algorithms::TerrainLodRenderPacket& renderPacket,
    const Algorithms::TerrainLodStats& stats,
    bool buildSucceeded)
{
    if (!buildSucceeded)
    {
        // 算法显式失败时不再继续解释 mesh 内容
        return false;
    }

    if (renderPacket.Mode == Algorithms::TerrainLodRenderMode::CpuMesh &&
        (renderPacket.CpuMesh.Vertices.empty() || renderPacket.CpuMesh.Indices.empty()))
    {
        // 当前 Classic 和 DOD 都必须输出 CPU mesh
        return false;
    }

    if (renderPacket.Mode == Algorithms::TerrainLodRenderMode::GpuBuffers &&
        (renderPacket.GpuVertexBufferId == 0U ||
         renderPacket.GpuIndexBufferId == 0U ||
         renderPacket.IndexCount == 0U))
    {
        return false;
    }

    if (renderPacket.Mode == Algorithms::TerrainLodRenderMode::GpuIndirect &&
        (renderPacket.GpuVertexBufferId == 0U ||
         renderPacket.GpuIndexBufferId == 0U ||
         renderPacket.IndirectDrawBufferId == 0U ||
         renderPacket.IndexCount == 0U))
    {
        return false;
    }

    if (stats.ActiveTriangleCount == 0U || stats.MaxActiveDepth > scenario.Settings.MaxDepth)
    {
        // max depth 越界通常说明算法没有正确遵守统一 settings
        return false;
    }

    return !scenario.RequireTopologyClean || !HasInvalidTopology(stats);
}

bool ValidateRunShape(const BenchmarkScenario& scenario, std::vector<BenchmarkFrameResult>& frames)
{
    bool passed = std::all_of(
        frames.begin(),
        frames.end(),
        [](const BenchmarkFrameResult& frame) {
            return frame.Passed;
        });

    if (scenario.RequireNearDetailIncrease && frames.size() >= 4U)
    {
        // Smoke 不比较绝对三角形数
        // 只要求近处视点相对 far 有明显细节增长
        const std::size_t farTriangles = frames[0].TriangleCount;
        // 这样 Classic 和 DOD 可在细节数完全一致时通过
        // GPU 初期实现也能在合理接近时扩展校验口径
        const bool centerHasMoreDetail = frames[1].TriangleCount > farTriangles * 2U;
        const bool cornerHasMoreDetail = frames[2].TriangleCount > farTriangles * 2U;
        const bool returnHasMoreDetail = frames[3].TriangleCount > farTriangles * 2U;
        frames[1].Passed = frames[1].Passed && centerHasMoreDetail;
        frames[2].Passed = frames[2].Passed && cornerHasMoreDetail;
        frames[3].Passed = frames[3].Passed && returnHasMoreDetail;
        passed = passed && centerHasMoreDetail && cornerHasMoreDetail && returnHasMoreDetail;
    }

    return passed;
}

BenchmarkAlgorithmRun RunAlgorithm(
    BenchmarkAlgorithmSelection selection,
    const BenchmarkScenario& scenario,
    const Terrain::HeightMap& heightMap)
{
    BenchmarkAlgorithmRun run{};
    run.AlgorithmName = ToString(selection);

    if (selection == BenchmarkAlgorithmSelection::Gpu)
    {
        run.UnavailableReason = Algorithms::GpuRoam::GpuRoamLikeUnavailableReason();
        if (!run.UnavailableReason.empty())
        {
            return run;
        }
    }

    std::unique_ptr<Algorithms::ITerrainLodAlgorithm> algorithm = CreateAlgorithm(selection);
    if (algorithm == nullptr)
    {
        // all 模式下 unavailable 会被打印为 SKIP
        // 显式选择该算法时 RunTerrainLodBenchmark 会返回失败
        return run;
    }

    run.Available = true;
    const Algorithms::TerrainLodAlgorithmInfo info = algorithm->Info();
    // 输出使用算法自报名称
    // 这样 CSV 不依赖命令行别名
    run.AlgorithmName = std::string{info.Name};
    run.Frames.reserve(scenario.CameraPath.size());

    for (std::size_t index = 0; index < scenario.CameraPath.size(); ++index)
    {
        // 每帧都重新构造 BuildInput
        // 防止算法修改输入 settings 后污染后续帧
        const BenchmarkCameraKeyframe& camera = scenario.CameraPath[index];
        Algorithms::TerrainLodBuildInput buildInput{};
        buildInput.HeightMap = &heightMap;
        buildInput.CameraPosition = camera.Position;
        buildInput.Settings = scenario.Settings;

        Algorithms::TerrainLodRenderPacket renderPacket{};
        std::string errorMessage;
        const auto start = Clock::now();
        const bool buildSucceeded = algorithm->BuildRenderData(buildInput, renderPacket, &errorMessage);
        const auto end = Clock::now();
        if (!errorMessage.empty())
        {
            // 错误信息不吞掉
            // benchmark 输出需要能定位失败算法和帧
            std::cerr << "[" << info.Name << "] " << errorMessage << '\n';
        }

        const Algorithms::TerrainLodStats stats = algorithm->Stats();
        BenchmarkFrameResult frame{};
        frame.AlgorithmName = std::string{info.Name};
        frame.ProfileName = scenario.Name;
        frame.CameraName = camera.Name;
        frame.FrameIndex = static_cast<int>(index);
        frame.TimeSeconds = camera.TimeSeconds;
        frame.CameraPosition = camera.Position;
        frame.HeightMapWidth = heightMap.Width();
        frame.HeightMapHeight = heightMap.Height();
        frame.VertexCount = renderPacket.Mode == Algorithms::TerrainLodRenderMode::CpuMesh ?
            renderPacket.CpuMesh.Vertices.size() :
            renderPacket.ActiveTriangleCount * 3U;
        frame.IndexCount = renderPacket.IndexCount;
        frame.TriangleCount = stats.ActiveTriangleCount;
        frame.Stats = stats;
        // BuildWallMilliseconds 包括接口调用外层开销
        // Stats.CpuUpdateMilliseconds 则由算法自己报告
        frame.BuildWallMilliseconds = std::chrono::duration<float, std::milli>(end - start).count();
        frame.Passed = ValidateFrame(scenario, renderPacket, stats, buildSucceeded);
        run.Frames.push_back(frame);
    }

    run.Passed = ValidateRunShape(scenario, run.Frames);
    return run;
}

float Median(std::vector<float> values)
{
    if (values.empty())
    {
        return 0.0F;
    }

    std::sort(values.begin(), values.end());
    // 当前样本量较小
    // 使用上中位数足够支撑 smoke 和 standard 摘要
    return values[values.size() / 2U];
}

float Percentile95(std::vector<float> values)
{
    if (values.empty())
    {
        return 0.0F;
    }

    std::sort(values.begin(), values.end());
    // p95 使用 ceiling 选择保守样本
    // 避免短路径下把最大 spike 过早平滑掉
    const std::size_t index = static_cast<std::size_t>(
        std::ceil(static_cast<float>(values.size()) * 0.95F) - 1.0F);
    return values[std::min(index, values.size() - 1U)];
}

void PrintRunSummary(const BenchmarkAlgorithmRun& run)
{
    if (!run.Available)
    {
        std::cout << "[SKIP] " << run.AlgorithmName << " unavailable";
        if (!run.UnavailableReason.empty())
        {
            std::cout << ": " << run.UnavailableReason;
        }
        std::cout << '\n';
        return;
    }

    std::vector<float> updateTimes;
    updateTimes.reserve(run.Frames.size());

    std::size_t minTriangles = 0U;
    std::size_t maxTriangles = 0U;
    if (!run.Frames.empty())
    {
        minTriangles = run.Frames.front().TriangleCount;
        maxTriangles = run.Frames.front().TriangleCount;
    }

    for (const BenchmarkFrameResult& frame : run.Frames)
    {
        updateTimes.push_back(frame.Stats.CpuUpdateMilliseconds);
        minTriangles = std::min(minTriangles, frame.TriangleCount);
        maxTriangles = std::max(maxTriangles, frame.TriangleCount);

        if (run.Frames.size() <= 8U)
        {
            // 短 profile 打印逐帧摘要
            // 长 profile 只输出聚合指标避免日志干扰性能阅读
            std::cout << (frame.Passed ? "[PASS] " : "[FAIL] ")
                      << run.AlgorithmName
                      << '/' << frame.CameraName
                      << " triangles=" << frame.TriangleCount
                      << " activeNodes=" << frame.Stats.ActiveNodeCount
                      << " split=" << frame.Stats.SplitCount
                      << " merge=" << frame.Stats.MergeCount
                      << " maxDepth=" << frame.Stats.MaxActiveDepth
                      << " updateMs=" << std::fixed << std::setprecision(3) << frame.Stats.CpuUpdateMilliseconds
                      << " wallMs=" << frame.BuildWallMilliseconds
                      << " invalid=" << (frame.Stats.TjunctionCount + frame.Stats.InvalidNeighborCount + frame.Stats.InvalidTopologyCount)
                      << '\n';
        }
    }

    std::cout << (run.Passed ? "[PASS] " : "[FAIL] ")
              << run.AlgorithmName
              << " frames=" << run.Frames.size()
              << " triangles=" << minTriangles << ".." << maxTriangles
              << " updateMsMedian=" << std::fixed << std::setprecision(3) << Median(updateTimes)
              << " updateMsP95=" << Percentile95(updateTimes)
              << '\n';
}

bool WriteCsv(
    const std::filesystem::path& csvPath,
    const BenchmarkScenario& scenario,
    const std::vector<BenchmarkAlgorithmRun>& runs)
{
    if (csvPath.empty())
    {
        // 没有传 csv 时 benchmark 只做控制台回归
        return true;
    }

    if (csvPath.has_parent_path())
    {
        std::filesystem::create_directories(csvPath.parent_path());
    }

    std::ofstream csv{csvPath};
    if (!csv.is_open())
    {
        std::cerr << "Failed to open benchmark CSV for writing: " << csvPath << '\n';
        return false;
    }

    // 表头覆盖三类算法的共同统计字段
    // GPU 字段现在可为 0，后续实现后不需要改 CSV 契约
    csv << "profile,algorithm,frameIndex,timeSeconds,cameraName,cameraX,cameraY,cameraZ,"
           "heightMapWidth,heightMapHeight,terrainSize,heightScale,maxDepth,splitThreshold,mergeThreshold,"
           "activeTriangleCount,activeNodeCount,splitCount,forcedSplitCount,mergeCount,candidatePeakCount,"
           "tjunctionCount,invalidNeighborCount,invalidTopologyCount,cpuWorkerCount,cpuUtilizationPercent,"
           "cpuUpdateMs,cpuErrorEvalMs,"
           "cpuDecisionMs,cpuTopologyMs,cpuCollectMs,cpuMeshBuildMs,cpuUploadMs,gpuComputeMs,renderMs,"
           "cpuGpuUploadBytes,cpuGpuReadbackBytes,buildWallMs,passed\n";

    for (const BenchmarkAlgorithmRun& run : runs)
    {
        if (!run.Available)
        {
            // CSV 只记录实际运行的算法
            // SKIP 已经在控制台摘要中呈现
            continue;
        }

        for (const BenchmarkFrameResult& frame : run.Frames)
        {
            // 字段顺序保持和表头一致
            // 新算法只要填 TerrainLodStats 就能复用同一 CSV
            csv << frame.ProfileName << ','
                << frame.AlgorithmName << ','
                << frame.FrameIndex << ','
                << frame.TimeSeconds << ','
                << frame.CameraName << ','
                << frame.CameraPosition.x << ','
                << frame.CameraPosition.y << ','
                << frame.CameraPosition.z << ','
                << frame.HeightMapWidth << ','
                << frame.HeightMapHeight << ','
                << scenario.Settings.TerrainSize << ','
                << scenario.Settings.HeightScale << ','
                << scenario.Settings.MaxDepth << ','
                << scenario.Settings.SplitThreshold << ','
                << scenario.Settings.MergeThreshold << ','
                << frame.TriangleCount << ','
                << frame.Stats.ActiveNodeCount << ','
                << frame.Stats.SplitCount << ','
                << frame.Stats.ForcedSplitCount << ','
                << frame.Stats.MergeCount << ','
                << frame.Stats.CandidatePeakCount << ','
                << frame.Stats.TjunctionCount << ','
                << frame.Stats.InvalidNeighborCount << ','
                << frame.Stats.InvalidTopologyCount << ','
                << frame.Stats.CpuWorkerCount << ','
                << frame.Stats.CpuUtilizationPercent << ','
                << frame.Stats.CpuUpdateMilliseconds << ','
                << frame.Stats.CpuErrorEvalMilliseconds << ','
                << frame.Stats.CpuDecisionMilliseconds << ','
                << frame.Stats.CpuTopologyMilliseconds << ','
                << frame.Stats.CpuCollectMilliseconds << ','
                << frame.Stats.CpuMeshBuildMilliseconds << ','
                << frame.Stats.CpuUploadMilliseconds << ','
                << frame.Stats.GpuComputeMilliseconds << ','
                << frame.Stats.RenderMilliseconds << ','
                << frame.Stats.CpuGpuUploadBytes << ','
                << frame.Stats.CpuGpuReadbackBytes << ','
                << frame.BuildWallMilliseconds << ','
                << (frame.Passed ? 1 : 0)
                << '\n';
        }
    }

    std::cout << "Benchmark CSV written: " << csvPath << '\n';
    return true;
}

bool ParseAlgorithm(std::string_view value, BenchmarkAlgorithmSelection& outSelection)
{
    // 接受少量别名
    // 方便脚本里使用短名
    // 也兼容接口内部算法名
    if (value == "classic" || value == "classic_cpu_roam")
    {
        outSelection = BenchmarkAlgorithmSelection::Classic;
        return true;
    }

    if (value == "dod" || value == "data-oriented" || value == "data_oriented")
    {
        outSelection = BenchmarkAlgorithmSelection::DataOriented;
        return true;
    }

    if (value == "gpu" || value == "gpu_roam")
    {
        outSelection = BenchmarkAlgorithmSelection::Gpu;
        return true;
    }

    if (value == "all")
    {
        outSelection = BenchmarkAlgorithmSelection::All;
        return true;
    }

    return false;
}

bool ParseProfile(std::string_view value, BenchmarkProfile& outProfile)
{
    // profile 名只保留 smoke 和 standard
    // 参数面保持小而稳定
    if (value == "smoke")
    {
        outProfile = BenchmarkProfile::Smoke;
        return true;
    }

    if (value == "standard")
    {
        outProfile = BenchmarkProfile::Standard;
        return true;
    }

    return false;
}
} // 匿名命名空间

int RunTerrainLodBenchmark(const BenchmarkOptions& options)
{
    const BenchmarkScenario scenario = MakeScenario(options.Profile);

    Terrain::HeightMap heightMap;
    std::string errorMessage;
    if (!heightMap.LoadFromFile(scenario.HeightMapPath, &errorMessage))
    {
        // HeightMap 是所有算法共享输入
        // 加载失败时没有可比较的基准
        std::cerr << errorMessage << '\n';
        return 1;
    }

    std::cout << "Terrain LOD benchmark profile=" << scenario.Name
              << " algorithm=" << ToString(options.Algorithm)
              << " heightmap=" << scenario.HeightMapPath
              << " frames=" << scenario.CameraPath.size()
              << " maxDepth=" << scenario.Settings.MaxDepth
              << " splitThreshold=" << scenario.Settings.SplitThreshold
              << " mergeThreshold=" << scenario.Settings.MergeThreshold
              << '\n';

    std::vector<BenchmarkAlgorithmRun> runs;
    const std::vector<BenchmarkAlgorithmSelection> selections = ExpandAlgorithmSelection(options.Algorithm);
    runs.reserve(selections.size());

    bool anyAvailable = false;
    bool allAvailablePassed = true;
    for (BenchmarkAlgorithmSelection selection : selections)
    {
        // allAvailablePassed 只统计实际运行的算法
        // GPU 尚未实现时 all 仍可用于 Classic 和 DOD 回归
        BenchmarkAlgorithmRun run = RunAlgorithm(selection, scenario, heightMap);
        PrintRunSummary(run);
        anyAvailable = anyAvailable || run.Available;
        allAvailablePassed = allAvailablePassed && (!run.Available || run.Passed);
        runs.push_back(std::move(run));
    }

    const bool csvWritten = WriteCsv(options.CsvPath, scenario, runs);

    // all 模式允许 GPU 尚未实现时 skip
    // 但不能允许三种选择全都没有实际运行
    if (!anyAvailable)
    {
        // 显式选择未实现算法时需要失败
        // 否则 CI 会误把空跑当作通过
        std::cerr << "No requested benchmark algorithm is available.\n";
        return 1;
    }

    if (options.Algorithm != BenchmarkAlgorithmSelection::All && !runs.empty() && !runs.front().Available)
    {
        // 非 all 模式下 unavailable 不是 skip
        // 用户明确要求了该算法
        return 1;
    }

    const bool passed = allAvailablePassed && csvWritten;
    std::cout << (passed ? "Terrain LOD benchmark result: PASS\n" : "Terrain LOD benchmark result: FAIL\n");
    return passed ? 0 : 1;
}

int RunTerrainLodBenchmarkFromCommandLine(int argc, char** argv)
{
    BenchmarkOptions options{};

    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument{argv[index]};
        if (argument == "--benchmark")
        {
            // main 已经根据 --benchmark 分流
            // 这里允许重复看到该 flag
            continue;
        }

        if (argument == "--help" || argument == "-h")
        {
            // benchmark help 不启动窗口也不加载 HeightMap
            std::cout << BenchmarkUsage();
            return 0;
        }

        if (argument == "--algorithm" && index + 1 < argc)
        {
            // 所有带值参数都在消费后递增 index
            // 避免下一轮把值当成未知参数
            if (!ParseAlgorithm(argv[++index], options.Algorithm))
            {
                std::cerr << "Unknown benchmark algorithm: " << argv[index] << '\n';
                std::cerr << BenchmarkUsage();
                return 1;
            }
            continue;
        }

        if (argument == "--algorithm")
        {
            // 缺值错误需要在解析时返回
            // 不进入默认 benchmark
            std::cerr << "--algorithm requires a value.\n";
            std::cerr << BenchmarkUsage();
            return 1;
        }

        if (argument == "--profile" && index + 1 < argc)
        {
            // profile 控制场景和验收规则
            // 不允许静默 fallback 到 smoke
            if (!ParseProfile(argv[++index], options.Profile))
            {
                std::cerr << "Unknown benchmark profile: " << argv[index] << '\n';
                std::cerr << BenchmarkUsage();
                return 1;
            }
            continue;
        }

        if (argument == "--profile")
        {
            std::cerr << "--profile requires a value.\n";
            std::cerr << BenchmarkUsage();
            return 1;
        }

        if (argument == "--csv" && index + 1 < argc)
        {
            // CSV 路径可以指向尚不存在的父目录
            // WriteCsv 会负责创建
            options.CsvPath = argv[++index];
            continue;
        }

        if (argument == "--csv")
        {
            std::cerr << "--csv requires a path.\n";
            std::cerr << BenchmarkUsage();
            return 1;
        }

        std::cerr << "Unknown benchmark argument: " << argument << '\n';
        std::cerr << BenchmarkUsage();
        return 1;
    }

    // 命令行解析只负责轻量参数
    // 实际场景构造和算法运行集中在 RunTerrainLodBenchmark
    return RunTerrainLodBenchmark(options);
}

std::string BenchmarkUsage()
{
    return "Usage: ParallelROAM --benchmark [--algorithm classic|dod|gpu|all] [--profile smoke|standard] [--csv path]\n";
}
} // 命名空间 ParallelRoam::Benchmark
