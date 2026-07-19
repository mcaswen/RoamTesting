#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace ParallelRoam::Algorithms::GpuRoam
{
/// <summary>
/// split 和 merge 候选标记 pass 的资源绑定及决策阈值
/// </summary>
struct GpuRoamCandidateMarkingPassInput
{
    std::uint32_t ProgramId{0}; // 候选分类 program
    std::uint32_t NodeBufferId{0}; // 读取父子和邻接关系
    std::uint32_t ActiveLeafBufferId{0}; // 误差数组对应的叶索引
    std::uint32_t ScreenErrorBufferId{0}; // error pass 的评分输出
    std::uint32_t CounterBufferId{0}; // 两类候选数量的原子计数
    std::uint32_t SplitCandidateBufferId{0}; // 稠密 split 节点索引
    std::uint32_t MergeCandidateBufferId{0}; // 稠密 merge 父节点索引
    std::uint32_t HeightMapTextureId{0}; // merge 对称评分的采样源
    std::size_t NodeCount{0}; // 防御性节点索引上界
    std::size_t ActiveLeafLimit{0}; // 活动叶输入有效长度
    int MaxDepth{0}; // split 深度硬上限
    float TerrainSize{0.0F}; // 世界空间地形边长
    float HeightScale{0.0F}; // 世界空间高度幅度
    float DistanceScale{0.0F}; // 相机距离权重
    float SplitThreshold{0.0F}; // 高于该值进入 split 集合
    float MergeThreshold{0.0F}; // 两个兄弟均低于该值才进入 merge 集合
    glm::vec3 CameraPosition{0.0F}; // 世界空间评分视点
};

[[nodiscard]] bool EnsureGpuRoamCandidateMarkingProgram(
    std::uint32_t& programId,
    std::string* errorMessage);

void RunGpuRoamCandidateMarkingPass(const GpuRoamCandidateMarkingPassInput& input);
} // namespace ParallelRoam::Algorithms::GpuRoam
