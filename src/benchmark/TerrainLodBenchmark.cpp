#include "benchmark/TerrainLodBenchmark.h"

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/classic_roam/ClassicRoamTerrainLodAlgorithm.h"
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

struct BenchmarkCameraKeyframe
{
    std::string Name;
    glm::vec3 Position{0.0F};
    float TimeSeconds{0.0F};
};

struct BenchmarkScenario
{
    std::string Name;
    std::filesystem::path HeightMapPath;
    Algorithms::TerrainLodSettings Settings;
    std::vector<BenchmarkCameraKeyframe> CameraPath;
    bool RequireTopologyClean{false};
    bool RequireNearDetailIncrease{false};
};

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
    float BuildWallMilliseconds{0.0F};
    bool Passed{false};
};

struct BenchmarkAlgorithmRun
{
    std::string AlgorithmName;
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
    std::vector<BenchmarkCameraKeyframe> path;
    path.reserve(64);

    constexpr int FrameCount = 64;
    constexpr float Radius = 22.0F;
    constexpr float Height = 7.5F;
    constexpr float DurationSeconds = 16.0F;

    for (int index = 0; index < FrameCount; ++index)
    {
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
    scenario.Name = ToString(profile);
    scenario.Settings.TerrainSize = 30.0F;
    scenario.Settings.HeightScale = 4.0F;
    scenario.Settings.MaxDepth = 14;
    scenario.Settings.SplitThreshold = 0.04F;
    scenario.Settings.MergeThreshold = 0.02F;
    scenario.Settings.DistanceScale = 24.0F;
    scenario.Settings.SplitBudget = 8192;
    scenario.Settings.EnableLocalConstraints = true;

    if (profile == BenchmarkProfile::Smoke)
    {
        scenario.HeightMapPath = "assets/heightmaps/Hm_Terrain_Test_129.pgm";
        scenario.Settings.EnableTopologyValidation = true;
        scenario.RequireTopologyClean = true;
        scenario.RequireNearDetailIncrease = true;
        scenario.CameraPath = {
            BenchmarkCameraKeyframe{"far", glm::vec3{0.0F, 14.0F, 28.0F}, 0.0F},
            BenchmarkCameraKeyframe{"center", glm::vec3{0.0F, 4.0F, 0.0F}, 1.0F},
            BenchmarkCameraKeyframe{"near-corner", glm::vec3{-13.0F, 2.5F, -13.0F}, 2.0F},
            BenchmarkCameraKeyframe{"center-return", glm::vec3{0.0F, 4.0F, 0.0F}, 3.0F},
        };
        return scenario;
    }

    scenario.HeightMapPath = "assets/heightmaps/Hm_Terrain_Peking_513.png";
    scenario.Settings.EnableTopologyValidation = false;
    scenario.RequireTopologyClean = false;
    scenario.RequireNearDetailIncrease = false;
    scenario.CameraPath = MakeStandardCameraPath();
    return scenario;
}

std::unique_ptr<Algorithms::ITerrainLodAlgorithm> CreateAlgorithm(BenchmarkAlgorithmSelection selection)
{
    if (selection == BenchmarkAlgorithmSelection::Classic)
    {
        return std::make_unique<Algorithms::ClassicRoam::ClassicRoamTerrainLodAlgorithm>();
    }

    return nullptr;
}

std::vector<BenchmarkAlgorithmSelection> ExpandAlgorithmSelection(BenchmarkAlgorithmSelection selection)
{
    if (selection != BenchmarkAlgorithmSelection::All)
    {
        return {selection};
    }

    return {
        BenchmarkAlgorithmSelection::Classic,
        BenchmarkAlgorithmSelection::DataOriented,
        BenchmarkAlgorithmSelection::Gpu,
    };
}

bool HasInvalidTopology(const Algorithms::TerrainLodStats& stats)
{
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
        return false;
    }

    if (renderPacket.Mode == Algorithms::TerrainLodRenderMode::CpuMesh &&
        (renderPacket.CpuMesh.Vertices.empty() || renderPacket.CpuMesh.Indices.empty()))
    {
        return false;
    }

    if (stats.ActiveTriangleCount == 0U || stats.MaxActiveDepth > scenario.Settings.MaxDepth)
    {
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
        const std::size_t farTriangles = frames[0].TriangleCount;
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

    std::unique_ptr<Algorithms::ITerrainLodAlgorithm> algorithm = CreateAlgorithm(selection);
    if (algorithm == nullptr)
    {
        return run;
    }

    run.Available = true;
    const Algorithms::TerrainLodAlgorithmInfo info = algorithm->Info();
    run.AlgorithmName = std::string{info.Name};
    run.Frames.reserve(scenario.CameraPath.size());

    for (std::size_t index = 0; index < scenario.CameraPath.size(); ++index)
    {
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
        frame.VertexCount = renderPacket.CpuMesh.Vertices.size();
        frame.IndexCount = renderPacket.IndexCount;
        frame.TriangleCount = stats.ActiveTriangleCount;
        frame.Stats = stats;
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
    return values[values.size() / 2U];
}

float Percentile95(std::vector<float> values)
{
    if (values.empty())
    {
        return 0.0F;
    }

    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(
        std::ceil(static_cast<float>(values.size()) * 0.95F) - 1.0F);
    return values[std::min(index, values.size() - 1U)];
}

void PrintRunSummary(const BenchmarkAlgorithmRun& run)
{
    if (!run.Available)
    {
        std::cout << "[SKIP] " << run.AlgorithmName << " unavailable\n";
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

    csv << "profile,algorithm,frameIndex,timeSeconds,cameraName,cameraX,cameraY,cameraZ,"
           "heightMapWidth,heightMapHeight,terrainSize,heightScale,maxDepth,splitThreshold,mergeThreshold,"
           "activeTriangleCount,activeNodeCount,splitCount,forcedSplitCount,mergeCount,candidatePeakCount,"
           "tjunctionCount,invalidNeighborCount,invalidTopologyCount,cpuUpdateMs,cpuErrorEvalMs,"
           "cpuDecisionMs,cpuTopologyMs,cpuCollectMs,cpuMeshBuildMs,cpuUploadMs,gpuComputeMs,renderMs,"
           "cpuGpuUploadBytes,cpuGpuReadbackBytes,buildWallMs,passed\n";

    for (const BenchmarkAlgorithmRun& run : runs)
    {
        if (!run.Available)
        {
            continue;
        }

        for (const BenchmarkFrameResult& frame : run.Frames)
        {
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
        BenchmarkAlgorithmRun run = RunAlgorithm(selection, scenario, heightMap);
        PrintRunSummary(run);
        anyAvailable = anyAvailable || run.Available;
        allAvailablePassed = allAvailablePassed && (!run.Available || run.Passed);
        runs.push_back(std::move(run));
    }

    const bool csvWritten = WriteCsv(options.CsvPath, scenario, runs);

    if (!anyAvailable)
    {
        std::cerr << "No requested benchmark algorithm is available.\n";
        return 1;
    }

    if (options.Algorithm != BenchmarkAlgorithmSelection::All && !runs.empty() && !runs.front().Available)
    {
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
            continue;
        }

        if (argument == "--help" || argument == "-h")
        {
            std::cout << BenchmarkUsage();
            return 0;
        }

        if (argument == "--algorithm" && index + 1 < argc)
        {
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
            std::cerr << "--algorithm requires a value.\n";
            std::cerr << BenchmarkUsage();
            return 1;
        }

        if (argument == "--profile" && index + 1 < argc)
        {
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

    return RunTerrainLodBenchmark(options);
}

std::string BenchmarkUsage()
{
    return "Usage: ParallelROAM --benchmark [--algorithm classic|dod|gpu|all] [--profile smoke|standard] [--csv path]\n";
}
} // 命名空间 ParallelRoam::Benchmark
