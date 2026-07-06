#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace ParallelRoam::Algorithms::GpuRoam
{
struct GpuRoamCandidateMarkingPassInput
{
    std::uint32_t ProgramId{0};
    std::uint32_t NodeBufferId{0};
    std::uint32_t ActiveLeafBufferId{0};
    std::uint32_t ScreenErrorBufferId{0};
    std::uint32_t CounterBufferId{0};
    std::uint32_t SplitCandidateBufferId{0};
    std::uint32_t MergeCandidateBufferId{0};
    std::uint32_t HeightMapTextureId{0};
    std::size_t NodeCount{0};
    std::size_t ActiveLeafLimit{0};
    int MaxDepth{0};
    float TerrainSize{0.0F};
    float HeightScale{0.0F};
    float DistanceScale{0.0F};
    float SplitThreshold{0.0F};
    float MergeThreshold{0.0F};
    glm::vec3 CameraPosition{0.0F};
};

[[nodiscard]] bool EnsureGpuRoamCandidateMarkingProgram(
    std::uint32_t& programId,
    std::string* errorMessage);

void RunGpuRoamCandidateMarkingPass(const GpuRoamCandidateMarkingPassInput& input);
} // namespace ParallelRoam::Algorithms::GpuRoam
