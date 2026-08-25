#include "algorithms/ITerrainLodAlgorithm.h"
#include "algorithms/cbt_2024/Cbt2024Baseline.h"

#include <cstddef>
#include <cstdint>
#include <iostream>

namespace
{
using ParallelRoam::Algorithms::TerrainLodGpuResourceLifetime;
using ParallelRoam::Algorithms::TerrainLodNativeResourceApi;
using ParallelRoam::Algorithms::TerrainLodRenderMode;
using ParallelRoam::Algorithms::TerrainLodRenderPacket;

TerrainLodRenderPacket MakeGpuPacket()
{
    constexpr std::size_t TriangleCapacity = 128U;
    constexpr std::size_t VertexStride = 52U;

    TerrainLodRenderPacket packet{};
    packet.Mode = TerrainLodRenderMode::GpuProceduralIndirect;
    packet.NativeResourceApi = TerrainLodNativeResourceApi::Direct3D12;
    packet.NativeVertexBuffer = 1U;
    packet.NativeActiveLeafBuffer = 2U;
    packet.NativeIndirectDrawBuffer = 3U;
    packet.GpuVertexBufferCapacityBytes = TriangleCapacity * 3U * VertexStride;
    packet.GpuVertexStrideBytes = VertexStride;
    packet.GpuActiveLeafBufferCapacityBytes = TriangleCapacity * sizeof(std::uint32_t);
    packet.GpuActiveLeafStrideBytes = sizeof(std::uint32_t);
    packet.GpuIndirectDrawBufferCapacityBytes = 10U * sizeof(std::uint32_t);
    packet.GpuIndirectDrawArgumentOffsetBytes = 0U;
    packet.GpuResourceLifetime = TerrainLodGpuResourceLifetime::UntilNextBuildOrReset;
    packet.GpuResourceGeneration = 1U;
    return packet;
}
} // namespace

int main()
{
    bool passed = true;

    TerrainLodRenderPacket packet = MakeGpuPacket();
    // GPU owns the live draw count. Zero CPU diagnostics must not suppress a valid indirect draw.
    packet.ActiveLeafCount = 0U;
    packet.ActiveTriangleCount = 0U;
    passed &= packet.HasConsistentResourceContract();

    TerrainLodRenderPacket invalidIndirect = packet;
    invalidIndirect.GpuIndirectDrawArgumentOffsetBytes =
        invalidIndirect.GpuIndirectDrawBufferCapacityBytes - sizeof(std::uint32_t);
    passed &= !invalidIndirect.HasConsistentResourceContract();

    TerrainLodRenderPacket undersizedVertices = packet;
    undersizedVertices.GpuVertexBufferCapacityBytes -= undersizedVertices.GpuVertexStrideBytes;
    passed &= !undersizedVertices.HasConsistentResourceContract();

    namespace Baseline = ParallelRoam::Algorithms::Cbt2024::OfficialBaselineV1;
    // 阶段 I 的身份和正式场景参数属于公开契约，避免后续研究变体静默覆盖基线。
    passed &= Baseline::AlgorithmKey == "cbt_2024_official_baseline_v1";
    passed &= Baseline::DefaultPath.SampleCount == 600U;
    passed &= Baseline::DefaultPath.WarmupFrameCount == 16U;
    passed &= Baseline::ExtremePath.SampleCount == 64U;
    passed &= Baseline::CapacityMatrix.size() == 4U;

    if (!passed)
    {
        std::cerr << "Terrain LOD GPU resource contract regression\n";
        return 1;
    }
    return 0;
}
