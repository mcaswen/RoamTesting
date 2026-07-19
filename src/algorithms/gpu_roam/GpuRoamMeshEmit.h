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
    std::uint32_t ProgramId{0}; // 网格展开 program
    std::uint32_t NodeBufferId{0}; // 最终拓扑节点池
    std::uint32_t ActiveLeafBufferId{0}; // 最终压缩叶索引
    std::uint32_t CounterBufferId{0}; // 活动叶数量和间接命令计数来源
    std::uint32_t VertexBufferId{0}; // 三顶点展开输出
    std::uint32_t IndexBufferId{0}; // 连续索引输出
    std::uint32_t IndirectDrawBufferId{0}; // DrawElementsIndirect 参数输出
    std::uint32_t HeightMapTextureId{0}; // 顶点高度和法线采样源
    std::size_t ActiveLeafCapacity{0}; // 输出三角形容量
    std::size_t NodeCapacity{0}; // 节点读取上界
    int MaxDepth{0}; // 定义域解码深度上界
    std::uint64_t BuildSequence{0}; // 防止消费其他 build 的节点
    float TerrainSize{0.0F}; // 定义域到世界空间的水平缩放
    float HeightScale{0.0F}; // 高度纹理到世界空间的垂直缩放
};

[[nodiscard]] bool EnsureGpuRoamMeshEmitProgram(
    std::uint32_t& programId,
    std::string* errorMessage);

void RunGpuRoamMeshEmitPass(const GpuRoamMeshEmitPassInput& input);
} // namespace ParallelRoam::Algorithms::GpuRoam
