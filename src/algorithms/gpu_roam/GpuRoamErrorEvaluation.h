#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ParallelRoam::Algorithms::GpuRoam
{
/// <summary>
/// 屏幕误差评估 pass 的输入资源和统一地形参数
/// </summary>
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
    glm::mat4 ViewProjection{1.0F};
    std::array<glm::vec4, 6U> FrustumPlanes{};
    std::uint32_t DrawableWidth{1U};
    std::uint32_t DrawableHeight{1U};
};

[[nodiscard]] bool EnsureGpuRoamErrorEvaluationProgram(
    std::uint32_t& programId,
    std::string* errorMessage);

void RunGpuRoamErrorEvaluationPass(const GpuRoamErrorEvaluationPassInput& input);
} // namespace ParallelRoam::Algorithms::GpuRoam
