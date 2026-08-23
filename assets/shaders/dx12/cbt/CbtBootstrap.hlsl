static const uint InvalidIndex = 0xffffffffu;
static const uint VisibleFlag = 0x1u;
static const uint ModifiedFlag = 0x2u;

// 五个 root constants 定位基础物理槽位、基础深度并控制平面世界尺寸
cbuffer CbtBootstrapConstants : register(b0)
{
    uint TotalElementCount;
    uint BaseElementOffset;
    uint BaseElementCount;
    float TerrainSize;
    uint BaseDepth;
};

struct CbtBisectorData
{
    // 字段顺序与 C++ 的 32 字节 CbtBisectorData 保持二进制一致
    uint SubdivisionPattern;
    uint3 Indices;
    uint ProblematicNeighbor;
    uint BisectorState;
    uint Flags;
    uint PropagationId;
};

struct TerrainVertex
{
    // 52 字节布局由 structured buffer stride 和程序化 VS 共同解释
    float3 Position;
    float3 Normal;
    float2 TexCoord;
    float Height;
    float3 DebugColor;
    float DebugHighlight;
};

StructuredBuffer<uint64_t> HeapIds : register(t0);
StructuredBuffer<CbtBisectorData> BisectorData : register(t1);
// 基础控制点仅用于首次或 terrain size 变化时重建平面几何
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
    // heap id 为零表示未分配物理槽位 因此无需扫描 OCBT rank
    if (physicalSlot >= TotalElementCount || HeapIds[physicalSlot] == 0)
    {
        return;
    }

    uint vertexOffset;
    // draw 顶点数同时充当 active list 的三倍 append 游标
    InterlockedAdd(DrawState[0], 3u, vertexOffset);
    ActiveIndices[vertexOffset / 3u] = physicalSlot;

    // visible 和 modified 是 active 的逐级子集 只有对应标志存在才继续追加
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
    // draw state 已由所有 Indexation 线程完成写入 UAV barrier 保证这里读取稳定
    const uint activeCount = DrawState[0] / 3u;
    // 三组 dispatch 分别覆盖活动槽位 活动四位置和本帧修改位置
    GeometryDispatch[0] = (activeCount + 63u) / 64u;
    GeometryDispatch[1] = 1u;
    GeometryDispatch[2] = 1u;
    GeometryDispatch[3] = (activeCount * 4u + 63u) / 64u;
    GeometryDispatch[4] = 1u;
    GeometryDispatch[5] = 1u;
    const uint modifiedCount = DrawState[8] / 4u;
    GeometryDispatch[6] = (modifiedCount + 63u) / 64u;
    GeometryDispatch[7] = 1u;
    GeometryDispatch[8] = 1u;
    // 显式活动数供下一帧分类协议使用 不参与当前 ExecuteIndirect
    DrawState[9] = activeCount;
}

float3 DebugColor(uint localBase)
{
    // 六个基础半边使用稳定颜色 便于观察 winding 和共享边
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
    // 基础半边数量很小 仍保留标准 64 线程入口以复用 dispatch helper
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
        // 子位置位于前三个容量平面 父位置单独位于第四个平面
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

    // 基础深度不消费父位置 仍写入有限值以保证分类缓冲完全初始化
    ClassificationPositions[TotalElementCount * 3u + physicalSlot] =
        (positions[0] + positions[1] + positions[2]) / 3.0;
    [unroll]
    for (uint outputVertex = 0; outputVertex < 3; ++outputVertex)
    {
        RenderVertices[physicalSlot * 3u + outputVertex] = vertices[outputVertex];
    }
}

uint CbtBootstrapHeapDepth(uint64_t heapId)
{
    const uint high = uint(heapId >> 32u);
    return high != 0u ? uint(firstbithigh(high)) + 33u : uint(firstbithigh(uint(heapId))) + 1u;
}

void CbtSplitPlaneTriangle(inout float3 p0, inout float3 p1, inout float3 p2, bool rightChild)
{
    const float3 previous0 = p0;
    const float3 previous1 = p1;
    const float3 previous2 = p2;
    const float3 midpoint = (previous0 + previous2) * 0.5;
    // 与官方 LEB splitting matrix 的 bit=0/1 两种排列逐行等价。
    p0 = rightChild ? previous1 : previous2;
    p1 = midpoint;
    p2 = rightChild ? previous0 : previous1;
}

bool CbtEvaluatePlaneTriangle(
    uint64_t heapId,
    out uint localBase,
    out float3 child0,
    out float3 child1,
    out float3 child2,
    out float3 parent0,
    out float3 parent1,
    out float3 parent2)
{
    localBase = 0u;
    child0 = child1 = child2 = 0.0;
    parent0 = parent1 = parent2 = 0.0;
    const uint depth = CbtBootstrapHeapDepth(heapId);
    if (heapId == 0u || depth < BaseDepth)
    {
        return false;
    }

    const uint subtreeDepth = depth - BaseDepth;
    const uint64_t firstBaseHeapId = uint64_t(1) << (BaseDepth - 1u);
    const uint64_t baseHeapId = heapId >> subtreeDepth;
    if (baseHeapId < firstBaseHeapId || baseHeapId - firstBaseHeapId >= BaseElementCount)
    {
        return false;
    }
    localBase = uint(baseHeapId - firstBaseHeapId);
    child0 = BaseControlPoints[localBase * 3u + 0u];
    child1 = BaseControlPoints[localBase * 3u + 1u];
    child2 = BaseControlPoints[localBase * 3u + 2u];
    parent0 = child0;
    parent1 = child1;
    parent2 = child2;
    [loop]
    for (uint remaining = subtreeDepth; remaining > 0u; --remaining)
    {
        parent0 = child0;
        parent1 = child1;
        parent2 = child2;
        const uint bit = remaining - 1u;
        CbtSplitPlaneTriangle(child0, child1, child2, ((heapId >> bit) & 1u) != 0u);
    }
    return true;
}

float3 CbtControlToWorld(float3 control)
{
    return float3((control.x - 0.5) * TerrainSize, control.y, (control.z - 0.5) * TerrainSize);
}

[numthreads(64, 1, 1)]
void CSBuildModifiedGeometry(uint modifiedOrdinal : SV_DispatchThreadID)
{
    const uint modifiedCount = DrawState[8] / 4u;
    if (modifiedOrdinal >= modifiedCount)
    {
        return;
    }
    const uint physicalSlot = ModifiedIndices[modifiedOrdinal];
    if (physicalSlot >= TotalElementCount || HeapIds[physicalSlot] == 0u)
    {
        return;
    }

    uint localBase;
    float3 child0;
    float3 child1;
    float3 child2;
    float3 parent0;
    float3 parent1;
    float3 parent2;
    const uint64_t heapId = HeapIds[physicalSlot];
    if (!CbtEvaluatePlaneTriangle(
            heapId,
            localBase,
            child0,
            child1,
            child2,
            parent0,
            parent1,
            parent2))
    {
        return;
    }

    const float3 childControl[3] = {child0, child1, child2};
    [unroll]
    for (uint vertexIndex = 0u; vertexIndex < 3u; ++vertexIndex)
    {
        const float3 control = childControl[vertexIndex];
        const float3 position = CbtControlToWorld(control);
        ClassificationPositions[physicalSlot * 3u + vertexIndex] = position;

        TerrainVertex vertex;
        vertex.Position = position;
        vertex.Normal = float3(0.0, 1.0, 0.0);
        vertex.TexCoord = control.xz;
        vertex.Height = control.y;
        vertex.DebugColor = DebugColor(localBase);
        vertex.DebugHighlight = 1.0;
        RenderVertices[physicalSlot * 3u + vertexIndex] = vertex;
    }

    // 分类父面积只需要与当前最长边相对的旧父顶点。
    const float3 parentControl = (heapId & 1u) == 0u ? parent0 : parent2;
    ClassificationPositions[TotalElementCount * 3u + physicalSlot] =
        CbtControlToWorld(parentControl);
}
