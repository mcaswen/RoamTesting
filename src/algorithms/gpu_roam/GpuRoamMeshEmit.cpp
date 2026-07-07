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
layout(local_size_x = 128) in;

struct NodeRecord
{
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
    NodeRecord nodes[];
};

layout(std430, binding = 1) readonly buffer ActiveLeafBuffer
{
    uint activeLeafIndices[];
};

layout(std430, binding = 3) readonly buffer CounterBuffer
{
    uint activeLeafCount;
    uint splitCandidateCount;
    uint mergeCandidateCount;
    uint reservedCounter;
    uint splitOnlyCommitCount;
    uint allocatedNodeCount;
};

layout(std430, binding = 6) buffer MeshVertexBuffer
{
    float meshVertices[];
};

layout(std430, binding = 7) buffer MeshIndexBuffer
{
    uint meshIndices[];
};

layout(std430, binding = 8) buffer IndirectDrawBuffer
{
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
    return texture(uHeightMap, clamp(uv, vec2(0.0), vec2(1.0))).r;
}

vec3 domainToWorld(vec2 uv)
{
    return vec3(
        (uv.x - 0.5) * uTerrainSize,
        sampleHeight(uv) * uHeightScale,
        (uv.y - 0.5) * uTerrainSize);
}

vec3 sampleNormal(vec2 uv)
{
    ivec2 textureSizeValue = textureSize(uHeightMap, 0);
    float stepU = 1.0 / float(max(textureSizeValue.x - 1, 1));
    float stepV = 1.0 / float(max(textureSizeValue.y - 1, 1));
    float left = sampleHeight(vec2(uv.x - stepU, uv.y));
    float right = sampleHeight(vec2(uv.x + stepU, uv.y));
    float down = sampleHeight(vec2(uv.x, uv.y - stepV));
    float up = sampleHeight(vec2(uv.x, uv.y + stepV));

    vec3 tangentX = vec3(stepU * 2.0 * uTerrainSize, (right - left) * uHeightScale, 0.0);
    vec3 tangentZ = vec3(0.0, (up - down) * uHeightScale, stepV * 2.0 * uTerrainSize);
    vec3 normal = cross(tangentZ, tangentX);
    if (dot(normal, normal) <= 0.00000001)
    {
        return vec3(0.0, 1.0, 0.0);
    }

    return normalize(normal);
}

bool buildIdMatches(uint low, uint high)
{
    return low == uBuildSequenceLow && high == uBuildSequenceHigh;
}

vec3 leafDebugColor(NodeRecord node)
{
    uint depth = node.mergeBuildAndDepth.z;
    uint flags = node.topology1.w;
    float depthRatio = clamp(float(depth) / float(max(uMaxDepth, 1u)), 0.0, 1.0);
    bool rebuilt = buildIdMatches(node.activatedAndSplitBuild.x, node.activatedAndSplitBuild.y) ||
        buildIdMatches(node.mergeBuildAndDepth.x, node.mergeBuildAndDepth.y);

    if (rebuilt)
    {
        const uint forcedSplitFlag = 1u << 1u;
        if ((flags & forcedSplitFlag) != 0u)
        {
            return mix(vec3(0.96, 0.34, 0.90), vec3(0.96, 0.16, 0.42), depthRatio);
        }

        return mix(vec3(1.0, 0.68, 0.15), vec3(1.0, 0.34, 0.10), depthRatio);
    }

    if (depth > 0u)
    {
        return mix(vec3(0.08, 0.72, 0.62), vec3(0.10, 0.34, 0.95), depthRatio);
    }

    return vec3(0.28, 0.34, 0.30);
}

float leafDebugHighlight(NodeRecord node)
{
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
    uint offset = vertexIndex * vertexFloatStride;
    float height = sampleHeight(uv);
    vec3 position = vec3((uv.x - 0.5) * uTerrainSize, height * uHeightScale, (uv.y - 0.5) * uTerrainSize);
    vec3 normal = sampleNormal(uv);

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
    return !any(isnan(uv)) &&
        !any(isinf(uv)) &&
        all(greaterThanEqual(uv, vec2(0.0))) &&
        all(lessThanEqual(uv, vec2(1.0)));
}

void main()
{
    uint leafSlot = gl_GlobalInvocationID.x;
    uint emitLeafCount = min(activeLeafCount, uActiveLeafLimit);
    if (leafSlot == 0u)
    {
        drawCommand[0] = emitLeafCount * 3u;
        drawCommand[1] = 1u;
        drawCommand[2] = 0u;
        drawCommand[3] = 0u;
        drawCommand[4] = 0u;
    }

    if (leafSlot >= emitLeafCount)
    {
        return;
    }

    uint nodeIndex = activeLeafIndices[leafSlot];
    uint readableNodeCount = min(allocatedNodeCount, uNodeCapacity);
    if (nodeIndex >= readableNodeCount)
    {
        writeDegenerateLeaf(leafSlot);
        return;
    }

    NodeRecord node = nodes[nodeIndex];
    const uint splitFlag = 1u << 0u;
    const uint activeLeafFlag = 1u << 2u;
    uint flags = node.topology1.w;
    vec2 uvs[3] = vec2[3](node.domainAAndB.xy, node.domainAAndB.zw, node.domainCAndErrors.xy);
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

    uint vertexBase = leafSlot * 3u;
    writeVertex(vertexBase + 0u, uvs[0], debugColor, debugHighlight);
    writeVertex(vertexBase + 1u, uvs[1], debugColor, debugHighlight);
    writeVertex(vertexBase + 2u, uvs[2], debugColor, debugHighlight);

    vec3 edge0 = domainToWorld(uvs[1]) - domainToWorld(uvs[0]);
    vec3 edge1 = domainToWorld(uvs[2]) - domainToWorld(uvs[0]);
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
    return EnsureGpuRoamComputeProgram(programId, MeshEmitComputeSource, "mesh emit", errorMessage);
}

void RunGpuRoamMeshEmitPass(const GpuRoamMeshEmitPassInput& input)
{
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, input.NodeBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, input.ActiveLeafBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, input.CounterBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, input.VertexBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, input.IndexBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, input.IndirectDrawBufferId);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, input.HeightMapTextureId);

    glUseProgram(input.ProgramId);
    SetGpuRoamProgramInt(input.ProgramId, "uHeightMap", 0);
    SetGpuRoamProgramUInt(input.ProgramId, "uActiveLeafLimit", static_cast<std::uint32_t>(input.ActiveLeafCapacity));
    SetGpuRoamProgramUInt(input.ProgramId, "uNodeCapacity", static_cast<std::uint32_t>(input.NodeCapacity));
    SetGpuRoamProgramUInt(input.ProgramId, "uMaxDepth", static_cast<std::uint32_t>(std::max(input.MaxDepth, 0)));
    SetGpuRoamProgramUInt(input.ProgramId, "uBuildSequenceLow", GpuRoamLow32(input.BuildSequence));
    SetGpuRoamProgramUInt(input.ProgramId, "uBuildSequenceHigh", GpuRoamHigh32(input.BuildSequence));
    SetGpuRoamProgramFloat(input.ProgramId, "uTerrainSize", input.TerrainSize);
    SetGpuRoamProgramFloat(input.ProgramId, "uHeightScale", input.HeightScale);
    glDispatchCompute(GpuRoamWorkGroupCount(input.ActiveLeafCapacity), 1U, 1U);
    glMemoryBarrier(
        GL_SHADER_STORAGE_BARRIER_BIT |
        GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT |
        GL_ELEMENT_ARRAY_BARRIER_BIT |
        GL_COMMAND_BARRIER_BIT |
        GL_BUFFER_UPDATE_BARRIER_BIT);
}
} // namespace ParallelRoam::Algorithms::GpuRoam
