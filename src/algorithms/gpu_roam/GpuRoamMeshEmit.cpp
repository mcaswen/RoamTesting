#include "algorithms/gpu_roam/GpuRoamMeshEmit.h"

#include "algorithms/gpu_roam/GpuRoamComputeSupport.h"

#include <glad/gl.h>

#include <algorithm>

namespace ParallelRoam::Algorithms::GpuRoam
{
namespace
{
constexpr const char* MeshEmitComputeSource = R"(
#version 430 core
// 一个 invocation 为一个 active leaf 生成独立三顶点
// 不共享顶点可避免并行去重和原子索引分配
layout(local_size_x = 128) in;

struct NodeRecord
{
    // emit 使用 domain topology flag build id 和 depth
    vec4 domainAAndB;
    vec4 domainCAndErrors;
    uvec4 topology0;
    uvec4 topology1;
    uvec4 pathAndCreatedBuild;
    uvec4 activatedAndSplitBuild;
    uvec4 mergeBuildAndDepth;
};

layout(std430, binding = 0) readonly buffer NodeBuffer
{
    // 物理 nodeIndex 必须小于 allocatedNodeCount 和 uNodeCapacity
    NodeRecord nodes[];
};

layout(std430, binding = 1) readonly buffer ActiveLeafBuffer
{
    // 稠密 leafSlot 同时决定输出顶点和索引区间
    uint activeLeafIndices[];
};

layout(std430, binding = 3) readonly buffer CounterBuffer
{
    // activeLeafCount 和 allocatedNodeCount 来自本轮 compute 链
    uint activeLeafCount;
    uint splitCandidateCount;
    uint mergeCandidateCount;
    uint remainingSplitBudget;
    uint splitOnlyCommitCount;
    uint allocatedNodeCount;
    uint budgetRejectedSplitCount;
    uint reservedCounter;
};

layout(std430, binding = 6) buffer MeshVertexBuffer
{
    // 顶点按 13 个 float 紧密打包，与 TerrainMeshVertex 字段顺序一致
    float meshVertices[];
};

layout(std430, binding = 7) buffer MeshIndexBuffer
{
    // 每个 leaf 固定写入三个 uint index
    uint meshIndices[];
};

layout(std430, binding = 8) buffer IndirectDrawBuffer
{
    // 前五个 uint 对应 DrawElementsIndirectCommand
    uint drawCommand[];
};

layout(binding = 0) uniform sampler2D uHeightMap;

uniform uint uActiveLeafLimit;
uniform uint uNodeCapacity;
uniform uint uMaxDepth;
uniform uint uBuildSequenceLow;
uniform uint uBuildSequenceHigh;
uniform float uTerrainSize;
uniform float uHeightScale;

const uint vertexFloatStride = 13u;

float sampleHeight(vec2 uv)
{
    // emit 必须与 error pass 重建出同一世界空间高度。
    // 否则评分使用的距离和最终绘制几何会出现半纹素偏差。
    ivec2 size = textureSize(uHeightMap, 0);
    // 归一化 UV 映射到 CPU 高度数组的离散坐标范围。
    vec2 pixel = clamp(uv, vec2(0.0), vec2(1.0)) * vec2(max(size - ivec2(1), ivec2(0)));
    ivec2 p0 = ivec2(floor(pixel));
    ivec2 p1 = min(p0 + ivec2(1), size - ivec2(1));
    vec2 weight = pixel - vec2(p0);
    // 法线有限差分也复用本函数，因此梯度与顶点高度采用同一采样器。
    float h00 = texelFetch(uHeightMap, p0, 0).r;
    float h10 = texelFetch(uHeightMap, ivec2(p1.x, p0.y), 0).r;
    float h01 = texelFetch(uHeightMap, ivec2(p0.x, p1.y), 0).r;
    float h11 = texelFetch(uHeightMap, p1, 0).r;
    return mix(mix(h00, h10, weight.x), mix(h01, h11, weight.x), weight.y);
}

vec3 domainToWorld(vec2 uv)
{
    // 仅为绕序判断求世界位置，正式顶点写入由 writeVertex 完成
    return vec3(
        (uv.x - 0.5) * uTerrainSize,
        sampleHeight(uv) * uHeightScale,
        (uv.y - 0.5) * uTerrainSize);
}

vec3 sampleNormal(vec2 uv)
{
    // texel 步长来自真实高度图尺寸，不假设正方形输入
    ivec2 textureSizeValue = textureSize(uHeightMap, 0);
    float stepU = 1.0 / float(max(textureSizeValue.x - 1, 1));
    float stepV = 1.0 / float(max(textureSizeValue.y - 1, 1));
    // 中心差分在边界依赖 sampleHeight 的 clamp 形成单边近似
    float left = sampleHeight(vec2(uv.x - stepU, uv.y));
    float right = sampleHeight(vec2(uv.x + stepU, uv.y));
    float down = sampleHeight(vec2(uv.x, uv.y - stepV));
    float up = sampleHeight(vec2(uv.x, uv.y + stepV));

    // 切向量同时应用地形尺寸和高度缩放，法线处于世界空间
    vec3 tangentX = vec3(stepU * 2.0 * uTerrainSize, (right - left) * uHeightScale, 0.0);
    vec3 tangentZ = vec3(0.0, (up - down) * uHeightScale, stepV * 2.0 * uTerrainSize);
    vec3 normal = cross(tangentZ, tangentX);
    // 零高度尺度或一像素纹理可能产生退化切向量
    if (dot(normal, normal) <= 0.00000001)
    {
        return vec3(0.0, 1.0, 0.0);
    }

    return normalize(normal);
}

bool buildIdMatches(uint low, uint high)
{
    // 低高位必须同时匹配，防止构建序列回绕时误标重建节点
    return low == uBuildSequenceLow && high == uBuildSequenceHigh;
}

vec3 leafDebugColor(NodeRecord node)
{
    // debug color 只编码本轮变化和深度，不影响几何或候选决策
    uint depth = node.mergeBuildAndDepth.z;
    uint flags = node.topology1.w;
    float depthRatio = clamp(float(depth) / float(max(uMaxDepth, 1u)), 0.0, 1.0);
    // activated 或 merge 发生在当前 build 都视为本轮重建
    bool rebuilt = buildIdMatches(node.activatedAndSplitBuild.x, node.activatedAndSplitBuild.y) ||
        buildIdMatches(node.mergeBuildAndDepth.x, node.mergeBuildAndDepth.y);

    if (rebuilt)
    {
        const uint forcedSplitFlag = 1u << 1u;
        // forced split 使用独立色系，便于观察兼容约束传播
        if ((flags & forcedSplitFlag) != 0u)
        {
            return mix(vec3(0.96, 0.34, 0.90), vec3(0.96, 0.16, 0.42), depthRatio);
        }

        return mix(vec3(1.0, 0.68, 0.15), vec3(1.0, 0.34, 0.10), depthRatio);
    }

    // 非本轮变化节点按深度从绿色过渡到蓝色
    if (depth > 0u)
    {
        return mix(vec3(0.08, 0.72, 0.62), vec3(0.10, 0.34, 0.95), depthRatio);
    }

    return vec3(0.28, 0.34, 0.30);
}

float leafDebugHighlight(NodeRecord node)
{
    // highlight 与颜色分离，使 pixel shader 可统一控制叠加强度
    uint depth = node.mergeBuildAndDepth.z;
    bool rebuilt = buildIdMatches(node.activatedAndSplitBuild.x, node.activatedAndSplitBuild.y) ||
        buildIdMatches(node.mergeBuildAndDepth.x, node.mergeBuildAndDepth.y);
    if (rebuilt)
    {
        return 1.0;
    }

    return depth > 0u ? 0.70 : 0.35;
}

void writeVertex(uint vertexIndex, vec2 uv, vec3 debugColor, float debugHighlight)
{
    // vertexIndex 映射到 float 数组起点，stride 必须与 C++ static_assert 一致
    uint offset = vertexIndex * vertexFloatStride;
    float height = sampleHeight(uv);
    vec3 position = vec3((uv.x - 0.5) * uTerrainSize, height * uHeightScale, (uv.y - 0.5) * uTerrainSize);
    vec3 normal = sampleNormal(uv);

    // 写入顺序固定为 position normal uv height debugColor highlight
    meshVertices[offset + 0u] = position.x;
    meshVertices[offset + 1u] = position.y;
    meshVertices[offset + 2u] = position.z;
    meshVertices[offset + 3u] = normal.x;
    meshVertices[offset + 4u] = normal.y;
    meshVertices[offset + 5u] = normal.z;
    meshVertices[offset + 6u] = uv.x;
    meshVertices[offset + 7u] = uv.y;
    meshVertices[offset + 8u] = height;
    meshVertices[offset + 9u] = debugColor.r;
    meshVertices[offset + 10u] = debugColor.g;
    meshVertices[offset + 11u] = debugColor.b;
    meshVertices[offset + 12u] = debugHighlight;
}

void writeDegenerateLeaf(uint leafSlot)
{
    // 非法节点仍占用固定输出槽，避免压缩阶段和 draw count 再次分叉
    // 三个相同索引形成零面积三角形，不会污染邻接几何
    uint vertexBase = leafSlot * 3u;
    writeVertex(vertexBase + 0u, vec2(0.0), vec3(1.0, 0.0, 1.0), 1.0);
    writeVertex(vertexBase + 1u, vec2(0.0), vec3(1.0, 0.0, 1.0), 1.0);
    writeVertex(vertexBase + 2u, vec2(0.0), vec3(1.0, 0.0, 1.0), 1.0);
    meshIndices[vertexBase + 0u] = vertexBase;
    meshIndices[vertexBase + 1u] = vertexBase;
    meshIndices[vertexBase + 2u] = vertexBase;
}

bool isValidDomain(vec2 uv)
{
    // 同时拒绝 NaN Inf 和零到一范围外坐标
    // 防御损坏 topology buffer 导致高度图和输出 buffer 未定义访问
    return !any(isnan(uv)) &&
        !any(isinf(uv)) &&
        all(greaterThanEqual(uv, vec2(0.0))) &&
        all(lessThanEqual(uv, vec2(1.0)));
}

void main()
{
    uint leafSlot = gl_GlobalInvocationID.x;
    // 实际计数和分配容量取较小值，间接 draw 永不越过输出 buffer
    uint emitLeafCount = min(activeLeafCount, uActiveLeafLimit);
    if (leafSlot == 0u)
    {
        // 单个 invocation 发布完整间接命令，其余 invocation 不写 command buffer
        drawCommand[0] = emitLeafCount * 3u;
        drawCommand[1] = 1u;
        drawCommand[2] = 0u;
        drawCommand[3] = 0u;
        drawCommand[4] = 0u;
    }

    // dispatch 按容量提交，超出实际 active 数量的线程立即退出
    if (leafSlot >= emitLeafCount)
    {
        return;
    }

    uint nodeIndex = activeLeafIndices[leafSlot];
    // allocatedNodeCount 由 topology pass 原子更新，capacity 是物理缓冲硬边界
    uint readableNodeCount = min(allocatedNodeCount, uNodeCapacity);
    if (nodeIndex >= readableNodeCount)
    {
        // 保持输出结构完整并以洋红色暴露错误节点
        writeDegenerateLeaf(leafSlot);
        return;
    }

    NodeRecord node = nodes[nodeIndex];
    const uint splitFlag = 1u << 0u;
    const uint activeLeafFlag = 1u << 2u;
    uint flags = node.topology1.w;
    vec2 uvs[3] = vec2[3](node.domainAAndB.xy, node.domainAAndB.zw, node.domainCAndErrors.xy);
    // compaction 后 topology 仍可能被同帧 split 改写，因此 emit 再做一次状态验证
    if ((flags & activeLeafFlag) == 0u ||
        (flags & splitFlag) != 0u ||
        !isValidDomain(uvs[0]) ||
        !isValidDomain(uvs[1]) ||
        !isValidDomain(uvs[2]))
    {
        writeDegenerateLeaf(leafSlot);
        return;
    }

    vec3 debugColor = leafDebugColor(node);
    float debugHighlight = leafDebugHighlight(node);

    // 每个 leaf 独占连续三顶点，不存在跨 invocation 写冲突
    uint vertexBase = leafSlot * 3u;
    writeVertex(vertexBase + 0u, uvs[0], debugColor, debugHighlight);
    writeVertex(vertexBase + 1u, uvs[1], debugColor, debugHighlight);
    writeVertex(vertexBase + 2u, uvs[2], debugColor, debugHighlight);

    vec3 edge0 = domainToWorld(uvs[1]) - domainToWorld(uvs[0]);
    vec3 edge1 = domainToWorld(uvs[2]) - domainToWorld(uvs[0]);
    // domain 绕序可能随二分路径翻转，按世界法线 Y 分量修正 index 顺序
    bool pointsTowardPositiveY = cross(edge0, edge1).y >= 0.0;

    meshIndices[vertexBase] = vertexBase;
    if (pointsTowardPositiveY)
    {
        meshIndices[vertexBase + 1u] = vertexBase + 1u;
        meshIndices[vertexBase + 2u] = vertexBase + 2u;
    }
    else
    {
        meshIndices[vertexBase + 1u] = vertexBase + 2u;
        meshIndices[vertexBase + 2u] = vertexBase + 1u;
    }
}
)";

} // namespace

bool EnsureGpuRoamMeshEmitProgram(std::uint32_t& programId, std::string* errorMessage)
{
    // emit 是 compute 链最后入口，编译失败时不能返回任何可绘制资源
    return EnsureGpuRoamComputeProgram(programId, MeshEmitComputeSource, "mesh emit", errorMessage);
}

void RunGpuRoamMeshEmitPass(const GpuRoamMeshEmitPassInput& input)
{
    // binding 6 到 8 是最终图形管线直接消费的输出资源
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, input.NodeBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, input.ActiveLeafBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, input.CounterBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, input.VertexBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, input.IndexBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, input.IndirectDrawBufferId);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, input.HeightMapTextureId);

    // capacity uniform 描述物理边界，counter 描述当前逻辑数量
    glUseProgram(input.ProgramId);
    SetGpuRoamProgramInt(input.ProgramId, "uHeightMap", 0);
    SetGpuRoamProgramUInt(input.ProgramId, "uActiveLeafLimit", static_cast<std::uint32_t>(input.ActiveLeafCapacity));
    SetGpuRoamProgramUInt(input.ProgramId, "uNodeCapacity", static_cast<std::uint32_t>(input.NodeCapacity));
    SetGpuRoamProgramUInt(input.ProgramId, "uMaxDepth", static_cast<std::uint32_t>(std::max(input.MaxDepth, 0)));
    SetGpuRoamProgramUInt(input.ProgramId, "uBuildSequenceLow", GpuRoamLow32(input.BuildSequence));
    SetGpuRoamProgramUInt(input.ProgramId, "uBuildSequenceHigh", GpuRoamHigh32(input.BuildSequence));
    SetGpuRoamProgramFloat(input.ProgramId, "uTerrainSize", input.TerrainSize);
    SetGpuRoamProgramFloat(input.ProgramId, "uHeightScale", input.HeightScale);
    // 按容量 dispatch 确保 slot0 总能写入零或非零 draw command
    glDispatchCompute(GpuRoamWorkGroupCount(input.ActiveLeafCapacity), 1U, 1U);
    // 输出下一步会分别作为 SSBO 顶点属性 index buffer 和 indirect command 使用
    // 每种消费方式都需要对应 barrier bit
    glMemoryBarrier(
        GL_SHADER_STORAGE_BARRIER_BIT |
        GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT |
        GL_ELEMENT_ARRAY_BARRIER_BIT |
        GL_COMMAND_BARRIER_BIT |
        GL_BUFFER_UPDATE_BARRIER_BIT);
}
} // namespace ParallelRoam::Algorithms::GpuRoam
