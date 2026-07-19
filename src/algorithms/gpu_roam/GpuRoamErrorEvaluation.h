#pragma once

#include <glm/glm.hpp>

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
    std::uint32_t ProgramId{0}; // 误差计算 program
    std::uint32_t NodeBufferId{0}; // 三角形定义域和几何误差来源
    std::uint32_t ActiveLeafBufferId{0}; // compaction 生成的叶索引
    std::uint32_t ScreenErrorBufferId{0}; // 按活动叶顺序写入的评分
    std::uint32_t HeightMapTextureId{0}; // 高度和局部起伏采样源
    std::size_t ActiveLeafCount{0}; // dispatch 的逻辑元素数
    float TerrainSize{0.0F}; // 单位定义域到世界空间的缩放
    float HeightScale{0.0F}; // 归一化高度到世界高度的缩放
    float DistanceScale{0.0F}; // 距离衰减强度
    glm::vec3 CameraPosition{0.0F}; // 世界空间相机位置
};

[[nodiscard]] bool EnsureGpuRoamErrorEvaluationProgram(
    std::uint32_t& programId,
    std::string* errorMessage);

void RunGpuRoamErrorEvaluationPass(const GpuRoamErrorEvaluationPassInput& input);
} // namespace ParallelRoam::Algorithms::GpuRoam
