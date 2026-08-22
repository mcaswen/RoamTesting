#include "CbtOccupancyTree.hlsli"

static const uint CbtInvalidIndex = 0xffffffffu;
static const uint CbtVisibleFlag = 0x1u;
static const uint CbtUnchangedElement = 0u;
static const uint CbtBisectElement = 1u;
static const uint CbtSimplifyElement = 2u;

// 分类返回值的数值和上游 ClassifyBisector ABI 完全一致。
// 负值同时承载不可见原因，正值只表示本帧请求 split。
static const int CbtBackFaceCulled = -3;
static const int CbtFrustumCulled = -2;
static const int CbtTooSmall = -1;
static const int CbtUnchanged = 0;
static const int CbtBisect = 1;

struct CbtBisectorData
{
    // Split 会把兼容链请求合并进 pattern；Reset/Classify 从零开始准备本帧。
    uint SubdivisionPattern;
    // 四种二分模板使用 indices 保存新物理槽位，不存逻辑 heapID。
    uint3 Indices;
    uint ProblematicNeighbor;
    uint BisectorState;
    uint Flags;
    uint PropagationId;
};

cbuffer CbtGlobalConstants : register(b0)
{
    // 使用列主矩阵，保持 GLM 上传内存与 HLSL mul(matrix, vector) 一致。
    column_major float4x4 CbtViewProjection;
};

cbuffer CbtGeometryConstants : register(b1)
{
    // total 包含动态池和位于尾部的六个基础槽位。
    uint CbtTotalElementCount;
    uint CbtBaseElementOffset;
    uint CbtBaseElementCount;
    uint CbtBaseDepth;
    uint CbtDynamicElementCount;
    // 最大深度逐帧上传，但不会改变已分配资源容量。
    uint CbtMaxSubdivisionDepth;
};

cbuffer CbtUpdateConstants : register(b2)
{
    // 面积阈值的单位是实际 drawable 像素，不复用 CPU ROAM 厚度误差。
    float CbtTriangleAreaPixels;
    float2 CbtScreenSize;
    float CbtUpdatePadding0;
    float3 CbtCameraPosition;
    float CbtUpdatePadding1;
    float3 CbtCameraForward;
    float CbtUpdatePadding2;
    float4 CbtFrustumPlanes[6];
};

StructuredBuffer<float3> CbtClassificationPositions : register(t0); // 三当前顶点后接每槽父级辅助点。
StructuredBuffer<uint> CbtPreviousActiveIndices : register(t1); // 顺序和上一帧 Indexation 输出一致。

// u0 和 u1 由 OCBT include 声明 其余寄存器严格延续上游 topology ABI
RWStructuredBuffer<uint64_t> CbtHeapIds : register(u2);
RWStructuredBuffer<uint3> CbtCurrentNeighbors : register(u3);
RWStructuredBuffer<uint3> CbtNextNeighbors : register(u4);
RWStructuredBuffer<CbtBisectorData> CbtBisectorDataBuffer : register(u5);
RWStructuredBuffer<uint> CbtClassification : register(u6);
RWStructuredBuffer<uint> CbtSimplification : register(u7);
RWStructuredBuffer<int> CbtAllocation : register(u8);
RWStructuredBuffer<int> CbtPropagation : register(u9);
// u10 到 u12 保存跨 pass 控制状态而不是按 element 展开的任务数组
RWStructuredBuffer<int> CbtMemory : register(u10);
RWStructuredBuffer<uint> CbtTopologyIndirect : register(u11);
RWStructuredBuffer<uint> CbtDrawState : register(u12);
RWStructuredBuffer<uint> CbtActiveIndices : register(u13);
RWStructuredBuffer<uint> CbtVisibleIndices : register(u14);
RWStructuredBuffer<uint> CbtModifiedIndices : register(u15);
// validation 只记录错误状态 不参与候选批准或拓扑提交
RWStructuredBuffer<uint> CbtValidation : register(u16);

[numthreads(1, 1, 1)]
void CSResetE0(uint dispatchThreadId : SV_DispatchThreadID)
{
    // memory[1] 只描述可分配动态池 基础六槽不进入 OCBT 容量
    CbtMemory[0] = 0;
    CbtMemory[1] = int(CbtElementCount - CbtReadTreeCount(1u));
    CbtClassification[0] = 0;
    CbtClassification[1] = 0;
    CbtSimplification[0] = 0;
    CbtAllocation[0] = 0;
    CbtPropagation[0] = 0;
    CbtPropagation[1] = 0;
    CbtValidation[0] = 0;
    CbtValidation[1] = 0xffffffffu;

    // 两个 draw command 保留 InstanceCount=1 其余计数由本帧 Indexation 重建
    CbtDrawState[0] = 0;
    CbtDrawState[1] = 1;
    CbtDrawState[2] = 0;
    CbtDrawState[3] = 0;
    CbtDrawState[4] = 0;
    CbtDrawState[5] = 1;
    CbtDrawState[6] = 0;
    CbtDrawState[7] = 0;
    // 尾部两个字分别是修改位置数和显式活动二分器数
    CbtDrawState[8] = 0;
    // 第九字保留上一帧活动数供本帧 Classify 间接调度

    [unroll]
    for (uint word = 0u; word < 9u; ++word)
    {
        CbtTopologyIndirect[word] = 0u;
    }
}

uint CbtHeapDepth(uint64_t heapId)
{
    // 上游深度采用 heapID 位长，因此基础 heapID 8..13 的深度都是 4。
    const uint high = uint(heapId >> 32u);
    return high != 0u ? uint(firstbithigh(high)) + 33u : uint(firstbithigh(uint(heapId))) + 1u;
}

bool CbtFrustumAabbIntersects(float3 aabbMin, float3 aabbMax)
{
    const float3 center = (aabbMax + aabbMin) * 0.5;
    const float3 extents = (aabbMax - aabbMin) * 0.5;
    // 上游分类只使用左右上下四个平面 近远裁剪继续由投影和背面条件处理
    [unroll]
    for (uint planeIndex = 0u; planeIndex < 4u; ++planeIndex)
    {
        const float4 plane = CbtFrustumPlanes[planeIndex];
        const float3 positiveVertex = center + extents * sign(plane.xyz);
        if (dot(positiveVertex, plane.xyz) + plane.w < 0.0)
        {
            return false;
        }
    }
    return true;
}

float2 CbtProjectToUnitScreen(float3 position)
{
    float4 projected = mul(CbtViewProjection, float4(position, 1.0));
    projected.xy /= projected.w;
    return projected.xy * 0.5 + 0.5;
}

float CbtProjectedArea(float2 p0, float2 p1, float2 p2)
{
    // 单位屏幕中的鞋带公式结果乘 drawable 面积后得到像素面积。
    const float normalizedArea = 0.5 * abs(
        p0.x * (p2.y - p1.y) +
        p1.x * (p0.y - p2.y) +
        p2.x * (p1.y - p0.y));
    return normalizedArea * CbtScreenSize.x * CbtScreenSize.y;
}

int CbtClassifyTriangle(float3 p0, float3 p1, float3 p2, float3 parentPosition, uint depth)
{
    // 裁剪、面积和迟滞判断的顺序也是协议的一部分，不能为优化而交换。
    const float3 triangleNormal = normalize(cross(p2 - p1, p0 - p1));
    const float3 triangleCenter = (p0 + p1 + p2) / 3.0;
    // 上游顶点是相机相对坐标 这里对世界坐标作等价视线变换
    const float3 viewDirection = normalize(CbtCameraPosition - triangleCenter);
    const float forwardDotView = dot(viewDirection, CbtCameraForward);
    const float viewDotNormal = dot(viewDirection, triangleNormal);
    if (forwardDotView < 0.0 && viewDotNormal < -1.0e-3)
    {
        return CbtBackFaceCulled;
    }

    const float3 aabbMin = min(min(p0, p1), p2);
    const float3 aabbMax = max(max(p0, p1), p2);
    if (!CbtFrustumAabbIntersects(aabbMin, aabbMax))
    {
        return CbtFrustumCulled;
    }

    const float2 projected0 = CbtProjectToUnitScreen(p0);
    const float2 projected1 = CbtProjectToUnitScreen(p1);
    const float2 projected2 = CbtProjectToUnitScreen(p2);
    // 掠射角放大保持上游公式 前序背面条件保证有效地形法线进入非负定义域
    const float areaOverestimation = lerp(2.0, 1.0, pow(viewDotNormal, 0.2));
    const float area = CbtProjectedArea(projected0, projected1, projected2) * areaOverestimation;
    if (CbtTriangleAreaPixels < area && depth < CbtMaxSubdivisionDepth)
    {
        return CbtBisect;
    }
    if ((CbtTriangleAreaPixels * 0.5 > area) || (depth > CbtMaxSubdivisionDepth))
    {
        // 当前叶过小时还要检查父三角形，防止在阈值附近产生 split/simplify 抖动。
        const float2 projectedParent = CbtProjectToUnitScreen(parentPosition);
        const float parentArea =
            CbtProjectedArea(projected0, projectedParent, projected2) * areaOverestimation;
        return ((CbtTriangleAreaPixels >= parentArea) || (depth > CbtMaxSubdivisionDepth))
            ? CbtTooSmall
            : CbtUnchanged;
    }
    return CbtUnchanged;
}

[numthreads(64, 1, 1)]
void CSClassify(uint activeOrdinal : SV_DispatchThreadID)
{
    // dispatch 向上取整，显式活动数负责裁掉最后一个 thread group 的空线程。
    if (activeOrdinal >= CbtDrawState[9])
    {
        return;
    }

    const uint physicalSlot = CbtPreviousActiveIndices[activeOrdinal];
    if (physicalSlot >= CbtTotalElementCount || CbtHeapIds[physicalSlot] == 0u)
    {
        CbtValidation[0] = 1u;
        InterlockedMin(CbtValidation[1], physicalSlot);
        return;
    }

    const float3 p0 = CbtClassificationPositions[3u * physicalSlot + 0u];
    const float3 p1 = CbtClassificationPositions[3u * physicalSlot + 1u];
    const float3 p2 = CbtClassificationPositions[3u * physicalSlot + 2u];
    const float3 parentPosition =
        CbtClassificationPositions[3u * CbtTotalElementCount + physicalSlot];
    const uint64_t heapId = CbtHeapIds[physicalSlot];
    const uint depth = CbtHeapDepth(heapId);
    const int validity = CbtClassifyTriangle(p0, p1, p2, parentPosition, depth);

    CbtBisectorData data = CbtBisectorDataBuffer[physicalSlot];
    data.SubdivisionPattern = 0u;
    data.BisectorState = CbtUnchangedElement;
    data.ProblematicNeighbor = CbtInvalidIndex;
    data.Flags = CbtVisibleFlag;

    if (validity > CbtUnchanged)
    {
        // split 候选从 classification[2] 开始紧密排列，头两个字保留原子计数。
        data.BisectorState = CbtBisectElement;
        uint targetSlot;
        InterlockedAdd(CbtClassification[0], 1u, targetSlot);
        if (targetSlot < CbtTotalElementCount)
        {
            CbtClassification[2u + targetSlot] = physicalSlot;
        }
        else
        {
            CbtValidation[0] = 2u;
            InterlockedMin(CbtValidation[1], physicalSlot);
        }
    }
    else
    {
        data.Flags = validity >= CbtTooSmall ? CbtVisibleFlag : 0u;
    }

    if (depth != CbtBaseDepth && validity < CbtUnchanged)
    {
        // simplify 表位于 2+T，且偶 heapID 只让一侧 sibling 发出一次 pair 请求。
        data.BisectorState = CbtSimplifyElement;
        if ((heapId & 1u) == 0u)
        {
            uint targetSlot;
            InterlockedAdd(CbtClassification[1], 1u, targetSlot);
            if (targetSlot < CbtTotalElementCount)
            {
                CbtClassification[2u + CbtTotalElementCount + targetSlot] = physicalSlot;
            }
            else
            {
                CbtValidation[0] = 3u;
                InterlockedMin(CbtValidation[1], physicalSlot);
            }
        }
    }
    CbtBisectorDataBuffer[physicalSlot] = data;
}

[numthreads(1, 1, 1)]
void CSPrepareClassificationIndirect(uint dispatchThreadId : SV_DispatchThreadID)
{
    // topology indirect 连续保存三组 D3D12_DISPATCH_ARGUMENTS。
    // 前两组分别消费 split 和 simplify 计数，第三组留给 E2 allocation。
    CbtTopologyIndirect[0] = (CbtClassification[0] + 63u) / 64u;
    CbtTopologyIndirect[1] = 1u;
    CbtTopologyIndirect[2] = 1u;
    CbtTopologyIndirect[3] = (CbtClassification[1] + 63u) / 64u;
    CbtTopologyIndirect[4] = 1u;
    CbtTopologyIndirect[5] = 1u;
    CbtTopologyIndirect[6] = 0u;
    CbtTopologyIndirect[7] = 1u;
    CbtTopologyIndirect[8] = 1u;
}

[numthreads(64, 1, 1)]
void CSReducePre(uint dispatchThreadId : SV_DispatchThreadID)
{
    // 生产入口与 GPU 对照测试调用同一个 include 实现 防止算法副本漂移
    CbtReducePre(dispatchThreadId);
}

[numthreads(64, 1, 1)]
void CSReduceFirst(uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    // 每个 group 独立归约一个固定大小 subtree
    CbtReduceFirst(groupId, groupIndex);
}

[numthreads(64, 1, 1)]
void CSReduceSecond(uint groupIndex : SV_GroupIndex)
{
    // 最后一组线程汇总所有 subtree 根并发布全局根计数
    CbtReduceSecond(groupIndex);
}
