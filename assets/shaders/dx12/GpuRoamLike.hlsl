struct NodeRecord
{
    float4 DomainAAndB;
    float4 DomainCAndErrors;
    uint4 Topology0;
    uint4 Topology1;
    uint4 PathAndCreatedBuild;
    uint4 ActivatedAndSplitBuild;
    uint4 MergeBuildAndDepth;
};

RWStructuredBuffer<NodeRecord> Nodes : register(u0);
RWStructuredBuffer<uint> ActiveLeaves : register(u1);
RWStructuredBuffer<float> ScreenErrors : register(u2);
RWStructuredBuffer<uint> Counters : register(u3);
RWStructuredBuffer<uint> SplitCandidates : register(u4);
RWStructuredBuffer<uint> MergeCandidates : register(u5);
RWStructuredBuffer<float> MeshVertices : register(u6);
RWStructuredBuffer<uint> MeshIndices : register(u7);
RWStructuredBuffer<uint> IndirectArgs : register(u8);
Texture2D<float> HeightMap : register(t0);
SamplerState HeightSampler : register(s0);

cbuffer GpuRoamConstants : register(b0)
{
    uint NodeCount;
    uint NodeCapacity;
    uint ActiveLeafLimit;
    uint MaxDepth;
    uint BuildSequenceLow;
    uint BuildSequenceHigh;
    uint HeightMapWidth;
    uint HeightMapHeight;
    float TerrainSize;
    float HeightScale;
    float DistanceScale;
    float SplitThreshold;
    float MergeThreshold;
    float3 CameraPosition;
};

static const uint InvalidNode = 0xffffffffu;
static const uint SplitFlag = 1u << 0u;
static const uint ForcedSplitFlag = 1u << 1u;
static const uint ActiveLeafFlag = 1u << 2u;
static const uint VertexFloatStride = 13u;

float SampleHeight(float2 uv)
{
    return HeightMap.SampleLevel(HeightSampler, saturate(uv), 0.0);
}

float3 DomainToWorld(float2 uv)
{
    return float3(
        (uv.x - 0.5) * TerrainSize,
        SampleHeight(uv) * HeightScale,
        (uv.y - 0.5) * TerrainSize);
}

float ScoreNode(uint nodeIndex)
{
    NodeRecord node = Nodes[nodeIndex];
    float2 aUv = node.DomainAAndB.xy;
    float2 bUv = node.DomainAAndB.zw;
    float2 cUv = node.DomainCAndErrors.xy;
    float3 a = DomainToWorld(aUv);
    float3 b = DomainToWorld(bUv);
    float3 c = DomainToWorld(cUv);
    float3 center = (a + b + c) / 3.0;
    float distanceToCamera = max(length(center - CameraPosition), 0.05);
    float safeDistanceScale = max(DistanceScale, 0.01);
    float normalizedDistance = safeDistanceScale / distanceToCamera;
    float weight = normalizedDistance * normalizedDistance;
    float worldError = node.DomainCAndErrors.z * HeightScale;
    float longestEdge = max(max(length(a - b), length(b - c)), length(c - a));
    return max(worldError * weight, longestEdge * 0.20 / safeDistanceScale * weight);
}

[numthreads(128, 1, 1)]
void CSCompact(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint nodeIndex = dispatchThreadId.x;
    uint readableNodeCount = min(NodeCapacity, Counters[5]);
    if (nodeIndex >= readableNodeCount)
    {
        return;
    }
    uint flags = Nodes[nodeIndex].Topology1.w;
    if ((flags & ActiveLeafFlag) == 0u || (flags & SplitFlag) != 0u)
    {
        return;
    }
    uint outputIndex;
    InterlockedAdd(Counters[0], 1u, outputIndex);
    if (outputIndex < NodeCapacity)
    {
        ActiveLeaves[outputIndex] = nodeIndex;
    }
}

[numthreads(128, 1, 1)]
void CSErrorEvaluation(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint leafSlot = dispatchThreadId.x;
    uint activeLeafCount = min(Counters[0], ActiveLeafLimit);
    if (leafSlot >= activeLeafCount)
    {
        return;
    }
    ScreenErrors[leafSlot] = ScoreNode(ActiveLeaves[leafSlot]);
}

[numthreads(128, 1, 1)]
void CSCandidateMarking(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint index = dispatchThreadId.x;
    uint activeLeafCount = min(Counters[0], ActiveLeafLimit);
    if (index < activeLeafCount)
    {
        uint nodeIndex = ActiveLeaves[index];
        uint depth = Nodes[nodeIndex].MergeBuildAndDepth.z;
        if (depth < MaxDepth && ScreenErrors[index] >= SplitThreshold)
        {
            uint outputIndex;
            InterlockedAdd(Counters[1], 1u, outputIndex);
            if (outputIndex < NodeCapacity)
            {
                SplitCandidates[outputIndex] = nodeIndex;
            }
        }
    }

    if (index >= NodeCount)
    {
        return;
    }
    if ((Nodes[index].Topology1.w & SplitFlag) == 0u)
    {
        return;
    }
    if (ScoreNode(index) <= MergeThreshold)
    {
        uint outputIndex;
        InterlockedAdd(Counters[2], 1u, outputIndex);
        if (outputIndex < NodeCapacity)
        {
            MergeCandidates[outputIndex] = index;
        }
    }
}

bool IsActiveLeaf(uint nodeIndex)
{
    uint flags = Nodes[nodeIndex].Topology1.w;
    return (flags & ActiveLeafFlag) != 0u && (flags & SplitFlag) == 0u;
}

bool MarkParentSplit(uint nodeIndex, out uint originalFlags)
{
    originalFlags = Nodes[nodeIndex].Topology1.w;
    if ((originalFlags & ActiveLeafFlag) == 0u || (originalFlags & SplitFlag) != 0u)
    {
        return false;
    }
    uint splitFlags = (originalFlags | SplitFlag) & ~ActiveLeafFlag;
    uint previous;
    InterlockedCompareExchange(Nodes[nodeIndex].Topology1.w, originalFlags, splitFlags, previous);
    return previous == originalFlags;
}

void RestoreParentLeaf(uint nodeIndex, uint originalFlags)
{
    uint splitFlags = (originalFlags | SplitFlag) & ~ActiveLeafFlag;
    uint ignored;
    InterlockedCompareExchange(Nodes[nodeIndex].Topology1.w, splitFlags, originalFlags, ignored);
}

bool AllocateNodes(uint count, out uint firstNode)
{
    [loop]
    for (uint attempt = 0u; attempt < 8u; ++attempt)
    {
        uint current;
        InterlockedAdd(Counters[5], 0u, current);
        if (current + count > NodeCapacity)
        {
            return false;
        }
        uint previous;
        InterlockedCompareExchange(Counters[5], current, current + count, previous);
        if (previous == current)
        {
            firstNode = current;
            return true;
        }
    }
    return false;
}

void WriteChildNode(
    uint childIndex,
    uint parentIndex,
    float2 domainA,
    float2 domainB,
    float2 domainC,
    uint depth,
    uint chunkId,
    bool forced)
{
    NodeRecord child;
    child.DomainAAndB = float4(domainA, domainB);
    child.DomainCAndErrors = float4(domainC, 0.0, 0.0);
    child.Topology0 = uint4(parentIndex, InvalidNode, InvalidNode, InvalidNode);
    child.Topology1 = uint4(InvalidNode, InvalidNode, chunkId, ActiveLeafFlag | (forced ? ForcedSplitFlag : 0u));
    child.PathAndCreatedBuild = uint4(0u, 0u, BuildSequenceLow, BuildSequenceHigh);
    child.ActivatedAndSplitBuild = uint4(BuildSequenceLow, BuildSequenceHigh, 0u, 0u);
    child.MergeBuildAndDepth = uint4(0u, 0u, depth, 0u);
    Nodes[childIndex] = child;
}

void WriteSplitChildren(uint parentIndex, uint firstChild, bool forced)
{
    NodeRecord parent = Nodes[parentIndex];
    float2 a = parent.DomainAAndB.xy;
    float2 b = parent.DomainAAndB.zw;
    float2 c = parent.DomainCAndErrors.xy;
    float2 midpoint = (a + b) * 0.5;
    uint childDepth = parent.MergeBuildAndDepth.z + 1u;
    uint chunkId = parent.Topology1.z;
    WriteChildNode(firstChild, parentIndex, c, a, midpoint, childDepth, chunkId, forced);
    WriteChildNode(firstChild + 1u, parentIndex, b, c, midpoint, childDepth, chunkId, forced);
    Nodes[parentIndex].Topology0.y = firstChild;
    Nodes[parentIndex].Topology0.z = firstChild + 1u;
    Nodes[parentIndex].ActivatedAndSplitBuild.z = BuildSequenceLow;
    Nodes[parentIndex].ActivatedAndSplitBuild.w = BuildSequenceHigh;
}

[numthreads(128, 1, 1)]
void CSSplitOnlyTopology(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint candidateSlot = dispatchThreadId.x;
    uint splitCandidateCount = min(Counters[1], ActiveLeafLimit);
    if (candidateSlot >= splitCandidateCount)
    {
        return;
    }
    uint nodeIndex = SplitCandidates[candidateSlot];
    if (nodeIndex == InvalidNode || nodeIndex >= NodeCapacity || !IsActiveLeaf(nodeIndex))
    {
        return;
    }
    NodeRecord candidate = Nodes[nodeIndex];
    if (candidate.MergeBuildAndDepth.z >= MaxDepth)
    {
        return;
    }

    uint baseNeighbor = candidate.Topology0.w;
    if (baseNeighbor == InvalidNode)
    {
        uint originalFlags;
        if (!MarkParentSplit(nodeIndex, originalFlags))
        {
            return;
        }
        uint firstChild;
        if (!AllocateNodes(2u, firstChild))
        {
            RestoreParentLeaf(nodeIndex, originalFlags);
            return;
        }
        WriteSplitChildren(nodeIndex, firstChild, false);
        InterlockedAdd(Counters[4], 1u);
        return;
    }

    if (baseNeighbor <= nodeIndex || baseNeighbor >= NodeCapacity)
    {
        return;
    }
    NodeRecord paired = Nodes[baseNeighbor];
    if (paired.Topology0.w != nodeIndex ||
        paired.Topology1.z != candidate.Topology1.z ||
        paired.MergeBuildAndDepth.z >= MaxDepth ||
        !IsActiveLeaf(baseNeighbor))
    {
        return;
    }

    uint originalFlags;
    uint pairedFlags;
    if (!MarkParentSplit(nodeIndex, originalFlags))
    {
        return;
    }
    if (!MarkParentSplit(baseNeighbor, pairedFlags))
    {
        RestoreParentLeaf(nodeIndex, originalFlags);
        return;
    }
    uint firstChild;
    if (!AllocateNodes(4u, firstChild))
    {
        RestoreParentLeaf(baseNeighbor, pairedFlags);
        RestoreParentLeaf(nodeIndex, originalFlags);
        return;
    }
    WriteSplitChildren(nodeIndex, firstChild, false);
    WriteSplitChildren(baseNeighbor, firstChild + 2u, true);
    InterlockedAdd(Counters[4], 2u);
}

[numthreads(1, 1, 1)]
void CSResetActiveLeafCount(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    Counters[0] = 0u;
}

float3 SampleNormal(float2 uv)
{
    float stepU = 1.0 / max(float(HeightMapWidth - 1u), 1.0);
    float stepV = 1.0 / max(float(HeightMapHeight - 1u), 1.0);
    float left = SampleHeight(float2(uv.x - stepU, uv.y));
    float right = SampleHeight(float2(uv.x + stepU, uv.y));
    float down = SampleHeight(float2(uv.x, uv.y - stepV));
    float up = SampleHeight(float2(uv.x, uv.y + stepV));
    float3 tangentX = float3(stepU * 2.0 * TerrainSize, (right - left) * HeightScale, 0.0);
    float3 tangentZ = float3(0.0, (up - down) * HeightScale, stepV * 2.0 * TerrainSize);
    float3 normal = cross(tangentZ, tangentX);
    return dot(normal, normal) <= 0.00000001 ? float3(0.0, 1.0, 0.0) : normalize(normal);
}

bool BuildIdMatches(uint low, uint high)
{
    return low == BuildSequenceLow && high == BuildSequenceHigh;
}

float3 LeafDebugColor(NodeRecord node)
{
    uint depth = node.MergeBuildAndDepth.z;
    float depthRatio = saturate(float(depth) / float(max(MaxDepth, 1u)));
    bool rebuilt = BuildIdMatches(node.ActivatedAndSplitBuild.x, node.ActivatedAndSplitBuild.y) ||
        BuildIdMatches(node.MergeBuildAndDepth.x, node.MergeBuildAndDepth.y);
    if (rebuilt)
    {
        if ((node.Topology1.w & ForcedSplitFlag) != 0u)
        {
            return lerp(float3(0.96, 0.34, 0.90), float3(0.96, 0.16, 0.42), depthRatio);
        }
        return lerp(float3(1.0, 0.68, 0.15), float3(1.0, 0.34, 0.10), depthRatio);
    }
    return depth > 0u
        ? lerp(float3(0.08, 0.72, 0.62), float3(0.10, 0.34, 0.95), depthRatio)
        : float3(0.28, 0.34, 0.30);
}

float LeafDebugHighlight(NodeRecord node)
{
    bool rebuilt = BuildIdMatches(node.ActivatedAndSplitBuild.x, node.ActivatedAndSplitBuild.y) ||
        BuildIdMatches(node.MergeBuildAndDepth.x, node.MergeBuildAndDepth.y);
    return rebuilt ? 1.0 : (node.MergeBuildAndDepth.z > 0u ? 0.70 : 0.35);
}

void WriteVertex(uint vertexIndex, float2 uv, float3 debugColor, float debugHighlight)
{
    uint offset = vertexIndex * VertexFloatStride;
    float height = SampleHeight(uv);
    float3 position = float3((uv.x - 0.5) * TerrainSize, height * HeightScale, (uv.y - 0.5) * TerrainSize);
    float3 normal = SampleNormal(uv);
    MeshVertices[offset + 0u] = position.x;
    MeshVertices[offset + 1u] = position.y;
    MeshVertices[offset + 2u] = position.z;
    MeshVertices[offset + 3u] = normal.x;
    MeshVertices[offset + 4u] = normal.y;
    MeshVertices[offset + 5u] = normal.z;
    MeshVertices[offset + 6u] = uv.x;
    MeshVertices[offset + 7u] = uv.y;
    MeshVertices[offset + 8u] = height;
    MeshVertices[offset + 9u] = debugColor.r;
    MeshVertices[offset + 10u] = debugColor.g;
    MeshVertices[offset + 11u] = debugColor.b;
    MeshVertices[offset + 12u] = debugHighlight;
}

void WriteDegenerateLeaf(uint leafSlot)
{
    uint vertexBase = leafSlot * 3u;
    WriteVertex(vertexBase + 0u, 0.0, float3(1.0, 0.0, 1.0), 1.0);
    WriteVertex(vertexBase + 1u, 0.0, float3(1.0, 0.0, 1.0), 1.0);
    WriteVertex(vertexBase + 2u, 0.0, float3(1.0, 0.0, 1.0), 1.0);
    MeshIndices[vertexBase + 0u] = vertexBase;
    MeshIndices[vertexBase + 1u] = vertexBase;
    MeshIndices[vertexBase + 2u] = vertexBase;
}

bool IsValidDomain(float2 uv)
{
    return !any(isnan(uv)) && !any(isinf(uv)) && all(uv >= 0.0) && all(uv <= 1.0);
}

[numthreads(128, 1, 1)]
void CSMeshEmit(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint leafSlot = dispatchThreadId.x;
    uint emitLeafCount = min(Counters[0], ActiveLeafLimit);
    if (leafSlot == 0u)
    {
        IndirectArgs[0] = emitLeafCount * 3u;
        IndirectArgs[1] = 1u;
        IndirectArgs[2] = 0u;
        IndirectArgs[3] = 0u;
        IndirectArgs[4] = 0u;
    }
    if (leafSlot >= emitLeafCount)
    {
        return;
    }

    uint nodeIndex = ActiveLeaves[leafSlot];
    if (nodeIndex >= min(Counters[5], NodeCapacity))
    {
        WriteDegenerateLeaf(leafSlot);
        return;
    }
    NodeRecord node = Nodes[nodeIndex];
    float2 uv0 = node.DomainAAndB.xy;
    float2 uv1 = node.DomainAAndB.zw;
    float2 uv2 = node.DomainCAndErrors.xy;
    uint flags = node.Topology1.w;
    if ((flags & ActiveLeafFlag) == 0u || (flags & SplitFlag) != 0u ||
        !IsValidDomain(uv0) || !IsValidDomain(uv1) || !IsValidDomain(uv2))
    {
        WriteDegenerateLeaf(leafSlot);
        return;
    }

    float3 debugColor = LeafDebugColor(node);
    float debugHighlight = LeafDebugHighlight(node);
    uint vertexBase = leafSlot * 3u;
    WriteVertex(vertexBase + 0u, uv0, debugColor, debugHighlight);
    WriteVertex(vertexBase + 1u, uv1, debugColor, debugHighlight);
    WriteVertex(vertexBase + 2u, uv2, debugColor, debugHighlight);
    float3 edge0 = DomainToWorld(uv1) - DomainToWorld(uv0);
    float3 edge1 = DomainToWorld(uv2) - DomainToWorld(uv0);
    bool positiveY = cross(edge0, edge1).y >= 0.0;
    MeshIndices[vertexBase] = vertexBase;
    MeshIndices[vertexBase + 1u] = positiveY ? vertexBase + 1u : vertexBase + 2u;
    MeshIndices[vertexBase + 2u] = positiveY ? vertexBase + 2u : vertexBase + 1u;
}
