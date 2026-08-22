static const uint InvalidIndex = 0xffffffffu;
static const uint VisibleFlag = 0x1u;
static const uint ModifiedFlag = 0x2u;

cbuffer CbtBootstrapConstants : register(b0)
{
    uint TotalElementCount;
    uint BaseElementOffset;
    uint BaseElementCount;
    float TerrainSize;
};

struct CbtBisectorData
{
    uint SubdivisionPattern;
    uint3 Indices;
    uint ProblematicNeighbor;
    uint BisectorState;
    uint Flags;
    uint PropagationId;
};

struct TerrainVertex
{
    float3 Position;
    float3 Normal;
    float2 TexCoord;
    float Height;
    float3 DebugColor;
    float DebugHighlight;
};

StructuredBuffer<uint64_t> HeapIds : register(t0);
StructuredBuffer<CbtBisectorData> BisectorData : register(t1);
StructuredBuffer<float3> BaseControlPoints : register(t2);
RWStructuredBuffer<uint> ActiveIndices : register(u0);
RWStructuredBuffer<uint> VisibleIndices : register(u1);
RWStructuredBuffer<uint> ModifiedIndices : register(u2);
RWStructuredBuffer<uint> DrawState : register(u3);
RWStructuredBuffer<uint> GeometryDispatch : register(u4);
RWStructuredBuffer<float3> ClassificationPositions : register(u5);
RWStructuredBuffer<TerrainVertex> RenderVertices : register(u6);

[numthreads(64, 1, 1)]
void CSIndexation(uint physicalSlot : SV_DispatchThreadID)
{
    if (physicalSlot >= TotalElementCount || HeapIds[physicalSlot] == 0)
    {
        return;
    }

    uint vertexOffset;
    InterlockedAdd(DrawState[0], 3u, vertexOffset);
    ActiveIndices[vertexOffset / 3u] = physicalSlot;

    const uint flags = BisectorData[physicalSlot].Flags;
    if ((flags & VisibleFlag) == 0)
    {
        return;
    }

    InterlockedAdd(DrawState[4], 3u, vertexOffset);
    VisibleIndices[vertexOffset / 3u] = physicalSlot;
    if ((flags & ModifiedFlag) == 0)
    {
        return;
    }

    uint positionOffset;
    InterlockedAdd(DrawState[8], 4u, positionOffset);
    ModifiedIndices[positionOffset / 4u] = physicalSlot;
}

[numthreads(1, 1, 1)]
void CSPrepareIndirect(uint dispatchThreadId : SV_DispatchThreadID)
{
    const uint activeCount = DrawState[0] / 3u;
    GeometryDispatch[0] = (activeCount + 63u) / 64u;
    GeometryDispatch[1] = 1u;
    GeometryDispatch[2] = 1u;
    GeometryDispatch[3] = (activeCount * 4u + 63u) / 64u;
    GeometryDispatch[4] = 1u;
    GeometryDispatch[5] = 1u;
    GeometryDispatch[6] = (DrawState[8] + 63u) / 64u;
    GeometryDispatch[7] = 1u;
    GeometryDispatch[8] = 1u;
    DrawState[9] = activeCount;
}

float3 DebugColor(uint localBase)
{
    static const float3 Colors[6] = {
        float3(0.08, 0.72, 0.62),
        float3(0.10, 0.52, 0.88),
        float3(0.45, 0.76, 0.24),
        float3(1.00, 0.58, 0.12),
        float3(0.90, 0.28, 0.24),
        float3(0.70, 0.34, 0.82)
    };
    return Colors[min(localBase, 5u)];
}

[numthreads(64, 1, 1)]
void CSBuildBaseGeometry(uint localBase : SV_DispatchThreadID)
{
    if (localBase >= BaseElementCount)
    {
        return;
    }

    const uint physicalSlot = BaseElementOffset + localBase;
    float3 positions[3];
    TerrainVertex vertices[3];
    [unroll]
    for (uint localVertex = 0; localVertex < 3; ++localVertex)
    {
        const float3 control = BaseControlPoints[localBase * 3u + localVertex];
        positions[localVertex] = float3(
            (control.x - 0.5) * TerrainSize,
            0.0,
            (control.z - 0.5) * TerrainSize);
        ClassificationPositions[physicalSlot * 3u + localVertex] = positions[localVertex];

        TerrainVertex vertex;
        vertex.Position = positions[localVertex];
        vertex.Normal = float3(0.0, 1.0, 0.0);
        vertex.TexCoord = control.xz;
        vertex.Height = 0.0;
        vertex.DebugColor = DebugColor(localBase);
        vertex.DebugHighlight = 0.75;
        vertices[localVertex] = vertex;
    }

    // 基础深度不会访问父位置；仍写入有限值，避免首帧分类读取未初始化数据
    ClassificationPositions[TotalElementCount * 3u + physicalSlot] =
        (positions[0] + positions[1] + positions[2]) / 3.0;
    [unroll]
    for (uint outputVertex = 0; outputVertex < 3; ++outputVertex)
    {
        RenderVertices[physicalSlot * 3u + outputVertex] = vertices[outputVertex];
    }
}
