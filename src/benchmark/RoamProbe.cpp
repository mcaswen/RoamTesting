#include "benchmark/RoamProbe.h"

#include "benchmark/TerrainLodBenchmark.h"

namespace ParallelRoam::Benchmark
{
int RunRoamProbe()
{
    BenchmarkOptions options{};
    options.Algorithm = BenchmarkAlgorithmSelection::Classic;
    options.Profile = BenchmarkProfile::Smoke;
    return RunTerrainLodBenchmark(options);
}
} // 命名空间 ParallelRoam::Benchmark
