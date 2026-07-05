#pragma once

#include <filesystem>
#include <string>

namespace ParallelRoam::Benchmark
{
enum class BenchmarkAlgorithmSelection
{
    Classic,
    DataOriented,
    Gpu,
    All,
};

enum class BenchmarkProfile
{
    Smoke,
    Standard,
};

struct BenchmarkOptions
{
    BenchmarkAlgorithmSelection Algorithm{BenchmarkAlgorithmSelection::All};
    BenchmarkProfile Profile{BenchmarkProfile::Smoke};
    std::filesystem::path CsvPath;
};

/// <summary>
/// 运行三版本共享的无窗口地形 LOD benchmark
/// </summary>
[[nodiscard]] int RunTerrainLodBenchmark(const BenchmarkOptions& options);

/// <summary>
/// 从命令行解析少量 benchmark 参数并运行
/// </summary>
[[nodiscard]] int RunTerrainLodBenchmarkFromCommandLine(int argc, char** argv);

[[nodiscard]] std::string BenchmarkUsage();
} // 命名空间 ParallelRoam::Benchmark
