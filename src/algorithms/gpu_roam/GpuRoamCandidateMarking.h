#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ParallelRoam::Algorithms::GpuRoam
{
enum class GpuRoamCandidateKind : std::uint32_t
{
    Split,
    Merge,
};

/// <summary>
/// split 和 merge 候选标记 pass 的资源绑定及决策阈值
/// </summary>
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
    float SplitThreshold{0.0F};
    float MergeThreshold{0.0F};
    glm::mat4 ViewProjection{1.0F};
    std::array<glm::vec4, 6U> FrustumPlanes{};
    std::uint32_t DrawableWidth{1U};
    std::uint32_t DrawableHeight{1U};
    GpuRoamCandidateKind Kind{GpuRoamCandidateKind::Split};
};

[[nodiscard]] bool EnsureGpuRoamCandidateMarkingProgram(
    std::uint32_t& programId,
    std::string* errorMessage);

void RunGpuRoamCandidateMarkingPass(const GpuRoamCandidateMarkingPassInput& input);
} // namespace ParallelRoam::Algorithms::GpuRoam
