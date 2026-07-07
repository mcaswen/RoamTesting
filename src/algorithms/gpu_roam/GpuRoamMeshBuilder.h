#pragma once

#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/gpu_roam/GpuRoamState.h"

#include <cstddef>
#include <string>

namespace ParallelRoam::Algorithms::GpuRoam
{
struct GpuRoamBufferSnapshot;

/// <summary>
/// GPU ROAM-like mesh builder，负责上传快照、执行 compute pipeline 并产出 GPU render packet
/// </summary>
class GpuRoamMeshBuilder
{
public:
    [[nodiscard]] bool Build(
        const GpuRoamBufferSnapshot& snapshot,
        const TerrainLodBuildInput& input,
        TerrainLodRenderPacket& outPacket,
        TerrainLodStats& inOutStats,
        std::string* errorMessage);

    void Reset();

private:
    [[nodiscard]] bool UploadSnapshot(
        const GpuRoamBufferSnapshot& snapshot,
        const Terrain::HeightMap& heightMap,
        std::size_t nodeCapacity,
        std::size_t& uploadBytes,
        float& cpuUploadMilliseconds,
        float& bufferAllocationMilliseconds,
        std::string* errorMessage);

    [[nodiscard]] bool RunGpuComputePipeline(
        const GpuRoamBufferSnapshot& snapshot,
        const TerrainLodBuildInput& input,
        std::size_t& uploadBytes,
        float& gpuComputeMilliseconds,
        std::size_t& readbackBytes,
        float& bufferAllocationMilliseconds,
        float& dispatchWallMilliseconds,
        float& queryWaitMilliseconds,
        float& readbackWaitMilliseconds,
        std::size_t& gpuActiveLeafCount,
        std::size_t& gpuNodeCount,
        std::size_t& gpuSplitOnlyCommitCount,
        std::string* errorMessage);

    GpuRoamState _state;
};
} // namespace ParallelRoam::Algorithms::GpuRoam
