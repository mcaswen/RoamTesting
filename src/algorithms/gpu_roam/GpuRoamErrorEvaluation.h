#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace ParallelRoam::Algorithms::GpuRoam
{
struct GpuRoamErrorEvaluationPassInput
{
    std::uint32_t ProgramId{0};
    std::uint32_t NodeBufferId{0};
    std::uint32_t ActiveLeafBufferId{0};
    std::uint32_t ScreenErrorBufferId{0};
    std::uint32_t HeightMapTextureId{0};
    std::size_t ActiveLeafCount{0};
    float TerrainSize{0.0F};
    float HeightScale{0.0F};
    float DistanceScale{0.0F};
    glm::vec3 CameraPosition{0.0F};
};

[[nodiscard]] bool EnsureGpuRoamErrorEvaluationProgram(
    std::uint32_t& programId,
    std::string* errorMessage);

void RunGpuRoamErrorEvaluationPass(const GpuRoamErrorEvaluationPassInput& input);
} // namespace ParallelRoam::Algorithms::GpuRoam
