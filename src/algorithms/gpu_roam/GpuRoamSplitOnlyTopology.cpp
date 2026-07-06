#include "algorithms/gpu_roam/GpuRoamSplitOnlyTopology.h"

#include <glad/gl.h>

#include <algorithm>
#include <string>
#include <string_view>

namespace ParallelRoam::Algorithms::GpuRoam
{
namespace
{
constexpr GLuint InvalidProgramId = 0U;
constexpr GLuint LocalWorkGroupSize = 128U;

constexpr const char* SplitOnlyTopologyComputeSource = R"(
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

layout(std430, binding = 0) buffer NodeBuffer
{
    NodeRecord nodes[];
};

layout(std430, binding = 3) buffer CounterBuffer
{
    uint activeLeafCount;
    uint splitCandidateCount;
    uint mergeCandidateCount;
    uint reservedCounter;
    uint splitOnlyCommitCount;
    uint allocatedNodeCount;
};

layout(std430, binding = 4) readonly buffer SplitCandidateBuffer
{
    uint splitCandidates[];
};

uniform uint uNodeCapacity;
uniform uint uMaxDepth;
uniform uint uBuildSequenceLow;
uniform uint uBuildSequenceHigh;

const uint invalidNode = 0xffffffffu;
const uint splitFlag = 1u << 0u;
const uint activeLeafFlag = 1u << 2u;
const uint forcedSplitFlag = 1u << 1u;

bool isActiveLeaf(uint nodeIndex)
{
    uint flags = nodes[nodeIndex].topology1.w;
    return (flags & activeLeafFlag) != 0u && (flags & splitFlag) == 0u;
}

bool markParentSplit(uint nodeIndex)
{
    uint flags = nodes[nodeIndex].topology1.w;
    if ((flags & activeLeafFlag) == 0u || (flags & splitFlag) != 0u)
    {
        return false;
    }

    uint nextFlags = (flags | splitFlag) & ~activeLeafFlag;
    uint previous = atomicCompSwap(nodes[nodeIndex].topology1.w, flags, nextFlags);
    return previous == flags;
}

bool allocateNodes(uint count, out uint firstNode)
{
    for (uint attempt = 0u; attempt < 8u; ++attempt)
    {
        uint current = allocatedNodeCount;
        if (current + count > uNodeCapacity)
        {
            return false;
        }

        uint previous = atomicCompSwap(allocatedNodeCount, current, current + count);
        if (previous == current)
        {
            firstNode = current;
            return true;
        }
    }

    return false;
}

void writeChildNode(
    uint childIndex,
    uint parentIndex,
    vec2 domainA,
    vec2 domainB,
    vec2 domainC,
    uint depth,
    uint chunkId,
    bool forced)
{
    NodeRecord child;
    child.domainAAndB = vec4(domainA, domainB);
    child.domainCAndErrors = vec4(domainC, 0.0, 0.0);
    child.topology0 = uvec4(parentIndex, invalidNode, invalidNode, invalidNode);
    child.topology1 = uvec4(invalidNode, invalidNode, chunkId, activeLeafFlag | (forced ? forcedSplitFlag : 0u));
    child.pathAndCreatedBuild = uvec4(0u, 0u, uBuildSequenceLow, uBuildSequenceHigh);
    child.activatedAndSplitBuild = uvec4(uBuildSequenceLow, uBuildSequenceHigh, 0u, 0u);
    child.mergeBuildAndDepth = uvec4(0u, 0u, depth, 0u);
    nodes[childIndex] = child;
}

void writeSplitChildren(uint parentIndex, uint firstChild, bool forced)
{
    NodeRecord parent = nodes[parentIndex];
    vec2 a = parent.domainAAndB.xy;
    vec2 b = parent.domainAAndB.zw;
    vec2 c = parent.domainCAndErrors.xy;
    vec2 midpoint = (a + b) * 0.5;
    uint childDepth = parent.mergeBuildAndDepth.z + 1u;
    uint chunkId = parent.topology1.z;

    writeChildNode(firstChild, parentIndex, c, a, midpoint, childDepth, chunkId, forced);
    writeChildNode(firstChild + 1u, parentIndex, b, c, midpoint, childDepth, chunkId, forced);
    nodes[parentIndex].topology0.y = firstChild;
    nodes[parentIndex].topology0.z = firstChild + 1u;
    nodes[parentIndex].activatedAndSplitBuild.z = uBuildSequenceLow;
    nodes[parentIndex].activatedAndSplitBuild.w = uBuildSequenceHigh;
}

void main()
{
    uint candidateSlot = gl_GlobalInvocationID.x;
    if (candidateSlot >= splitCandidateCount)
    {
        return;
    }

    uint nodeIndex = splitCandidates[candidateSlot];
    if (nodeIndex == invalidNode || nodeIndex >= uNodeCapacity || !isActiveLeaf(nodeIndex))
    {
        return;
    }

    NodeRecord candidate = nodes[nodeIndex];
    if (candidate.mergeBuildAndDepth.z >= uMaxDepth)
    {
        return;
    }

    uint baseNeighbor = candidate.topology0.w;
    if (baseNeighbor == invalidNode)
    {
        uint firstChild = 0u;
        if (!allocateNodes(2u, firstChild) || !markParentSplit(nodeIndex))
        {
            return;
        }

        writeSplitChildren(nodeIndex, firstChild, false);
        atomicAdd(splitOnlyCommitCount, 1u);
        return;
    }

    if (baseNeighbor <= nodeIndex || baseNeighbor >= uNodeCapacity)
    {
        return;
    }

    NodeRecord paired = nodes[baseNeighbor];
    if (paired.topology0.w != nodeIndex ||
        paired.topology1.z != candidate.topology1.z ||
        paired.mergeBuildAndDepth.z >= uMaxDepth ||
        !isActiveLeaf(baseNeighbor))
    {
        return;
    }

    uint firstChild = 0u;
    if (!allocateNodes(4u, firstChild))
    {
        return;
    }

    if (!markParentSplit(nodeIndex) || !markParentSplit(baseNeighbor))
    {
        return;
    }

    writeSplitChildren(nodeIndex, firstChild, false);
    writeSplitChildren(baseNeighbor, firstChild + 2u, true);
    atomicAdd(splitOnlyCommitCount, 2u);
}
)";

std::uint32_t Low32(std::uint64_t value)
{
    return static_cast<std::uint32_t>(value & 0xFFFFFFFFULL);
}

std::uint32_t High32(std::uint64_t value)
{
    return static_cast<std::uint32_t>(value >> 32U);
}

GLuint WorkGroupCount(std::size_t itemCount)
{
    if (itemCount == 0U)
    {
        return 1U;
    }

    return static_cast<GLuint>((itemCount + LocalWorkGroupSize - 1U) / LocalWorkGroupSize);
}

std::string ReadShaderLog(GLuint shaderId)
{
    GLint logLength = 0;
    glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &logLength);
    if (logLength <= 1)
    {
        return {};
    }

    std::string log(static_cast<std::size_t>(logLength), '\0');
    GLsizei written = 0;
    glGetShaderInfoLog(shaderId, logLength, &written, log.data());
    log.resize(static_cast<std::size_t>(std::max(written, 0)));
    return log;
}

std::string ReadProgramLog(GLuint programId)
{
    GLint logLength = 0;
    glGetProgramiv(programId, GL_INFO_LOG_LENGTH, &logLength);
    if (logLength <= 1)
    {
        return {};
    }

    std::string log(static_cast<std::size_t>(logLength), '\0');
    GLsizei written = 0;
    glGetProgramInfoLog(programId, logLength, &written, log.data());
    log.resize(static_cast<std::size_t>(std::max(written, 0)));
    return log;
}

bool EnsureSplitOnlyProgram(std::uint32_t& programId, std::string* errorMessage)
{
    if (programId != InvalidProgramId)
    {
        return true;
    }

    const GLuint shaderId = glCreateShader(GL_COMPUTE_SHADER);
    const GLchar* shaderSource = SplitOnlyTopologyComputeSource;
    glShaderSource(shaderId, 1, &shaderSource, nullptr);
    glCompileShader(shaderId);

    GLint shaderCompiled = GL_FALSE;
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &shaderCompiled);
    if (shaderCompiled != GL_TRUE)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "GPU ROAM-like compute shader compile failed (split-only topology):\n" +
                            ReadShaderLog(shaderId);
        }
        glDeleteShader(shaderId);
        return false;
    }

    const GLuint nextProgramId = glCreateProgram();
    glAttachShader(nextProgramId, shaderId);
    glLinkProgram(nextProgramId);
    glDeleteShader(shaderId);

    GLint programLinked = GL_FALSE;
    glGetProgramiv(nextProgramId, GL_LINK_STATUS, &programLinked);
    if (programLinked != GL_TRUE)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "GPU ROAM-like compute program link failed (split-only topology):\n" +
                            ReadProgramLog(nextProgramId);
        }
        glDeleteProgram(nextProgramId);
        return false;
    }

    programId = nextProgramId;
    return true;
}

void SetProgramUInt(GLuint programId, const char* name, std::uint32_t value)
{
    const GLint location = glGetUniformLocation(programId, name);
    if (location >= 0)
    {
        glUniform1ui(location, value);
    }
}
} // namespace

bool RunGpuRoamSplitOnlyTopologyPass(
    std::uint32_t& programId,
    const GpuRoamSplitOnlyTopologyPassInput& input,
    std::string* errorMessage)
{
    if (input.NodeBufferId == 0U ||
        input.SplitCandidateBufferId == 0U ||
        input.CounterBufferId == 0U ||
        input.NodeCapacity == 0U)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "GPU ROAM-like split-only topology pass has incomplete buffers";
        }
        return false;
    }

    if (!EnsureSplitOnlyProgram(programId, errorMessage))
    {
        return false;
    }

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, input.NodeBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, input.CounterBufferId);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, input.SplitCandidateBufferId);

    glUseProgram(programId);
    SetProgramUInt(programId, "uNodeCapacity", static_cast<std::uint32_t>(input.NodeCapacity));
    SetProgramUInt(programId, "uMaxDepth", static_cast<std::uint32_t>(std::max(input.MaxDepth, 0)));
    SetProgramUInt(programId, "uBuildSequenceLow", Low32(input.BuildSequence));
    SetProgramUInt(programId, "uBuildSequenceHigh", High32(input.BuildSequence));
    glDispatchCompute(WorkGroupCount(input.CandidateDispatchCount), 1U, 1U);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    return true;
}
} // namespace ParallelRoam::Algorithms::GpuRoam
