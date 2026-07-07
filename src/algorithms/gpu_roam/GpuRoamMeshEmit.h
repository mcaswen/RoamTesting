#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ParallelRoam::Algorithms::GpuRoam
{
/// <summary>
/// GPU mesh emit pass 输入，使用已有 node、active leaf 和 height map 生成可绘制 buffer
/// </summary>
struct GpuRoamMeshEmitPassInput
{
    std::uint32_t ProgramId{0};
    std::uint32_t NodeBufferId{0};
    std::uint32_t ActiveLeafBufferId{0};
    std::uint32_t CounterBufferId{0};
    std::uint32_t VertexBufferId{0};
    std::uint32_t IndexBufferId{0};
    std::uint32_t IndirectDrawBufferId{0};
    std::uint32_t HeightMapTextureId{0};
    std::size_t ActiveLeafCapacity{0};
    std::size_t NodeCapacity{0};
    int MaxDepth{0};
    std::uint64_t BuildSequence{0};
    float TerrainSize{0.0F};
    float HeightScale{0.0F};
};

[[nodiscard]] bool EnsureGpuRoamMeshEmitProgram(
    std::uint32_t& programId,
    std::string* errorMessage);

void RunGpuRoamMeshEmitPass(const GpuRoamMeshEmitPassInput& input);
} // namespace ParallelRoam::Algorithms::GpuRoam
