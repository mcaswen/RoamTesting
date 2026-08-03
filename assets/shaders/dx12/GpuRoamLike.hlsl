// 与 CPU 侧 GpuRoamNodeRecord 保持七个 16 字节向量的结构化布局
struct NodeRecord
{
    // xy 为 A 点参数坐标 zw 为 B 点参数坐标
    float4 DomainAAndB;
    // xy 为 C 点参数坐标 z 为几何误差 w 为 CPU 屏幕误差
    float4 DomainCAndErrors;
    // parent leftChild rightChild baseNeighbor
    uint4 Topology0;
    // leftNeighbor rightNeighbor chunkId flags
    uint4 Topology1;
    // pathId 和 createdBuildId 的高低 32 位
    uint4 PathAndCreatedBuild;
    // activatedBuildId 和 splitBuildId 的高低 32 位
    uint4 ActivatedAndSplitBuild;
    // mergeBuildId 高低 32 位 depth 和保留字段
    uint4 MergeBuildAndDepth;
};

// u0 保存 CPU 快照并作为本次分裂可追加的节点池
RWStructuredBuffer<NodeRecord> Nodes : register(u0);
// u1 保存两次压缩得到的活动叶节点索引
RWStructuredBuffer<uint> ActiveLeaves : register(u1);
// u2 按活动叶槽位保存本次 GPU 误差
RWStructuredBuffer<float> ScreenErrors : register(u2);
// u3 以标量数组形式映射 GpuCounters
RWStructuredBuffer<uint> Counters : register(u3);
// u4 保存达到 split 阈值的节点索引
RWStructuredBuffer<uint> SplitCandidates : register(u4);
// u5 保存达到 merge 阈值的节点索引
RWStructuredBuffer<uint> MergeCandidates : register(u5);
// u6 按 13 个 float 展开 TerrainMeshVertex
RWStructuredBuffer<float> MeshVertices : register(u6);
// u7 保存每个叶节点独占的三个索引
RWStructuredBuffer<uint> MeshIndices : register(u7);
// u8 映射一个 D3D12_DRAW_INDEXED_ARGUMENTS
RWStructuredBuffer<uint> IndirectArgs : register(u8);
// t0 是归一化单通道浮点高度图
Texture2D<float> HeightMap : register(t0);
// clamp 采样保证边缘法向不会绕到对边
SamplerState HeightSampler : register(s0);

// 字段顺序必须与 CPU 侧 GpuConstants 一致
cbuffer GpuRoamConstants : register(b0)
{
    // CPU 快照已有节点数
    uint NodeCount;
    // 本帧节点池总容量
    uint NodeCapacity;
    // 活动叶与候选数组的写入上限
    uint ActiveLeafLimit;
    // 二叉三角形树最大深度
    uint MaxDepth;
    // 当前构建序列号低 32 位
    uint BuildSequenceLow;
    // 当前构建序列号高 32 位
    uint BuildSequenceHigh;
    // 高度图像素宽度
    uint HeightMapWidth;
    // 高度图像素高度
    uint HeightMapHeight;
    // 地形水平世界尺寸
    float TerrainSize;
    // 高度样本缩放
    float HeightScale;
    // split 候选像素阈值
    float SplitThreshold;
    // merge 候选像素阈值
    float MergeThreshold;
    // 最终活动 leaf 的统一硬上限
    uint TriangleBudget;
    // NDC 到像素坐标所需的 drawable 宽度
    uint DrawableWidth;
    // 像素投影尺度所需的 drawable 高度
    uint DrawableHeight;
    // 保持后续矩阵从 16 字节边界开始
    uint ConstantsReserved;
    // 世界空间到齐次裁剪空间
    float4x4 ViewProjection;
    // 向内法线的六个世界空间视锥平面
    float4 FrustumPlanes[6];
};

// 与 CPU 快照中的无效节点哨兵保持一致
static const uint InvalidNode = 0xffffffffu;
// 节点已经拥有子节点
static const uint SplitFlag = 1u << 0u;
// 节点由相邻三角形兼容性要求强制分裂
static const uint ForcedSplitFlag = 1u << 1u;
// 节点属于当前活动叶集合
static const uint ActiveLeafFlag = 1u << 2u;
// TerrainMeshVertex 由 13 个连续 float 组成
static const uint VertexFloatStride = 13u;

float SampleHeight(float2 uv)
{
    // 与 CPU HeightMap::SampleBilinear 相同，归一化坐标映射到 [0, size - 1]。
    uint maximumX = HeightMapWidth > 0u ? HeightMapWidth - 1u : 0u;
    uint maximumY = HeightMapHeight > 0u ? HeightMapHeight - 1u : 0u;
    float2 maximumPixel = float2(maximumX, maximumY);
    float2 pixel = saturate(uv) * maximumPixel;
    uint2 p0 = uint2(floor(pixel));
    uint2 p1 = min(p0 + uint2(1u, 1u), uint2(maximumX, maximumY));
    float2 weight = pixel - float2(p0);
    float h00 = HeightMap.Load(int3(p0, 0));
    float h10 = HeightMap.Load(int3(uint2(p1.x, p0.y), 0));
    float h01 = HeightMap.Load(int3(uint2(p0.x, p1.y), 0));
    float h11 = HeightMap.Load(int3(p1, 0));
    return lerp(lerp(h00, h10, weight.x), lerp(h01, h11, weight.x), weight.y);
}

float3 DomainToWorld(float2 uv)
{
    // 参数域中心映射到世界原点，Y 轴使用高度值
    return float3(
        (uv.x - 0.5) * TerrainSize,
        SampleHeight(uv) * HeightScale,
        (uv.y - 0.5) * TerrainSize);
}

bool IsNodeVisible(NodeRecord node, float3 a, float3 b, float3 c)
{
    float3 minimumPoint = min(a, min(b, c));
    float3 maximumPoint = max(a, max(b, c));
    float worldError = node.DomainCAndErrors.z * HeightScale;
    minimumPoint.y -= worldError;
    maximumPoint.y += worldError;
    float3 center = (minimumPoint + maximumPoint) * 0.5;
    float3 extents = (maximumPoint - minimumPoint) * 0.5;
    [unroll]
    for (uint planeIndex = 0u; planeIndex < 6u; ++planeIndex)
    {
        float4 plane = FrustumPlanes[planeIndex];
        float centerDistance = dot(plane.xyz, center) + plane.w;
        float projectedRadius = dot(abs(plane.xyz), extents);
        if (centerDistance + projectedRadius < 0.0)
        {
            return false;
        }
    }
    return true;
}

bool WedgieIntersectsNearPlane(float worldError, float3 a, float3 b, float3 c)
{
    // TerrainLodFrustumPlane::Near 的固定索引为 4。
    float4 nearPlane = FrustumPlanes[4];
    float thicknessRadius = abs(nearPlane.y) * worldError;
    return dot(nearPlane.xyz, a) + nearPlane.w <= thicknessRadius ||
        dot(nearPlane.xyz, b) + nearPlane.w <= thicknessRadius ||
        dot(nearPlane.xyz, c) + nearPlane.w <= thicknessRadius;
}

float ConservativeScreenDistortion(float worldError, float3 a, float3 b, float3 c)
{
    static const float ArtificialMaximumScreenError = 3.402823466e+38;
    static const float ProjectionEpsilon = 1.0e-7;
    if (WedgieIntersectsNearPlane(worldError, a, b, c))
    {
        return ArtificialMaximumScreenError;
    }

    float4 thicknessClip = mul(ViewProjection, float4(0.0, worldError, 0.0, 0.0));
    float3 vertices[3] = {a, b, c};
    float halfWidth = float(max(DrawableWidth, 1u)) * 0.5;
    float halfHeight = float(max(DrawableHeight, 1u)) * 0.5;
    float minimumDenominator = ArtificialMaximumScreenError;
    float maximumNumeratorSquared = 0.0;
    [unroll]
    for (uint vertexIndex = 0u; vertexIndex < 3u; ++vertexIndex)
    {
        float4 clip = mul(ViewProjection, float4(vertices[vertexIndex], 1.0));
        float denominator = clip.w * clip.w - thicknessClip.w * thicknessClip.w;
        if (!isfinite(denominator) || denominator <= ProjectionEpsilon)
        {
            return ArtificialMaximumScreenError;
        }
        float horizontal = halfWidth * (thicknessClip.x * clip.w - thicknessClip.w * clip.x);
        float vertical = halfHeight * (thicknessClip.y * clip.w - thicknessClip.w * clip.y);
        float numeratorSquared = horizontal * horizontal + vertical * vertical;
        if (!isfinite(numeratorSquared))
        {
            return ArtificialMaximumScreenError;
        }
        minimumDenominator = min(minimumDenominator, denominator);
        maximumNumeratorSquared = max(maximumNumeratorSquared, numeratorSquared);
    }
    return 2.0 * sqrt(maximumNumeratorSquared) / minimumDenominator;
}

float ProjectedLongestEdge(float3 a, float3 b, float3 c)
{
    static const float ArtificialMaximumScreenError = 3.402823466e+38;
    static const float ProjectionEpsilon = 1.0e-7;
    float3 vertices[3] = {a, b, c};
    float2 screenPositions[3];
    float2 halfDrawable = float2(
        float(max(DrawableWidth, 1u)) * 0.5,
        float(max(DrawableHeight, 1u)) * 0.5);
    [unroll]
    for (uint vertexIndex = 0u; vertexIndex < 3u; ++vertexIndex)
    {
        float4 clip = mul(ViewProjection, float4(vertices[vertexIndex], 1.0));
        if (!isfinite(clip.w) || abs(clip.w) <= ProjectionEpsilon)
        {
            return ArtificialMaximumScreenError;
        }
        screenPositions[vertexIndex] = halfDrawable * clip.xy / clip.w;
        if (!all(isfinite(screenPositions[vertexIndex])))
        {
            return ArtificialMaximumScreenError;
        }
    }
    return max(
        max(length(screenPositions[0] - screenPositions[1]), length(screenPositions[1] - screenPositions[2])),
        length(screenPositions[2] - screenPositions[0]));
}

float ScoreNode(uint nodeIndex)
{
    // 完整方差由 CPU DOD 快照写入 DomainCAndErrors.z，这里只做逐帧像素投影。
    NodeRecord node = Nodes[nodeIndex];
    float2 aUv = node.DomainAAndB.xy;
    float2 bUv = node.DomainAAndB.zw;
    float2 cUv = node.DomainCAndErrors.xy;
    float3 a = DomainToWorld(aUv);
    float3 b = DomainToWorld(bUv);
    float3 c = DomainToWorld(cUv);
    if (!IsNodeVisible(node, a, b, c))
    {
        return 0.0;
    }
    float worldError = node.DomainCAndErrors.z * HeightScale;
    // 公式 (3) 的分子最大值和分母最小值分别从三个角点取得。
    float geometricBoundPixels = ConservativeScreenDistortion(worldError, a, b, c);
    if (geometricBoundPixels == 3.402823466e+38)
    {
        return geometricBoundPixels;
    }
    // edge-density 是项目额外项，使用真实端点投影而不是中心深度近似。
    return max(geometricBoundPixels, ProjectedLongestEdge(a, b, c) * 0.20);
}

// 扫描节点池并以原子追加方式压缩活动叶节点
[numthreads(128, 1, 1)]
void CSCompact(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint nodeIndex = dispatchThreadId.x;
    // Counters[5] 是 GPU 分裂后真实分配的节点池尾指针
    uint readableNodeCount = min(NodeCapacity, Counters[5]);
    if (nodeIndex >= readableNodeCount)
    {
        return;
    }
    // 已分裂父节点不再属于最终叶集合
    uint flags = Nodes[nodeIndex].Topology1.w;
    if ((flags & ActiveLeafFlag) == 0u || (flags & SplitFlag) != 0u)
    {
        return;
    }
    uint outputIndex;
    // Counters[0] 是活动叶输出长度
    InterlockedAdd(Counters[0], 1u, outputIndex);
    if (outputIndex < ActiveLeafLimit)
    {
        ActiveLeaves[outputIndex] = nodeIndex;
    }
}

// 对压缩后的活动叶节点并行计算误差
[numthreads(128, 1, 1)]
void CSErrorEvaluation(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint leafSlot = dispatchThreadId.x;
    uint activeLeafCount = min(Counters[0], min(ActiveLeafLimit, TriangleBudget));
    if (leafSlot >= activeLeafCount)
    {
        return;
    }
    // ScreenErrors 使用叶槽位而不是节点索引，保持连续访问
    ScreenErrors[leafSlot] = ScoreNode(ActiveLeaves[leafSlot]);
}

// 扫描活动叶并生成超过 split 阈值的候选。
[numthreads(128, 1, 1)]
void CSSplitCandidateMarking(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint index = dispatchThreadId.x;
    uint activeLeafCount = min(Counters[0], min(ActiveLeafLimit, TriangleBudget));
    if (index >= activeLeafCount)
    {
        return;
    }
    uint nodeIndex = ActiveLeaves[index];
    uint depth = Nodes[nodeIndex].MergeBuildAndDepth.z;
    if (depth < MaxDepth && ScreenErrors[index] > SplitThreshold)
    {
        uint outputIndex;
        // Counters[1] 是 split 候选输出长度
        InterlockedAdd(Counters[1], 1u, outputIndex);
        if (outputIndex < ActiveLeafLimit)
        {
            SplitCandidates[outputIndex] = nodeIndex;
        }
    }
}

// 对 split parent 重新评分并生成低于 merge 阈值的候选。当前 GPU-like
// 路径不提交这些 merge；持久拓扑的 merge 仍由 CPU DOD baseline 完成。
[numthreads(128, 1, 1)]
void CSMergeCandidateMarking(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint index = dispatchThreadId.x;
    // merge 候选只扫描 CPU 快照范围，不读取本次刚创建的子节点
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
        // Counters[2] 是 merge 候选输出长度
        InterlockedAdd(Counters[2], 1u, outputIndex);
        if (outputIndex < NodeCapacity)
        {
            MergeCandidates[outputIndex] = index;
        }
    }
}

bool IsActiveLeaf(uint nodeIndex)
{
    // 活动叶标志与 split 标志共同定义可细分节点
    uint flags = Nodes[nodeIndex].Topology1.w;
    return (flags & ActiveLeafFlag) != 0u && (flags & SplitFlag) == 0u;
}

bool MarkParentSplit(uint nodeIndex, out uint originalFlags)
{
    // CAS 将父节点从活动叶原子切换为已分裂状态
    originalFlags = Nodes[nodeIndex].Topology1.w;
    if ((originalFlags & ActiveLeafFlag) == 0u || (originalFlags & SplitFlag) != 0u)
    {
        return false;
    }
    uint splitFlags = (originalFlags | SplitFlag) & ~ActiveLeafFlag;
    uint previous;
    // 多个候选竞争同一节点时只有一个线程取得所有权
    InterlockedCompareExchange(Nodes[nodeIndex].Topology1.w, originalFlags, splitFlags, previous);
    return previous == originalFlags;
}

void RestoreParentLeaf(uint nodeIndex, uint originalFlags)
{
    // 节点池分配失败时恢复候选提交前的活动叶状态
    uint splitFlags = (originalFlags | SplitFlag) & ~ActiveLeafFlag;
    uint ignored;
    InterlockedCompareExchange(Nodes[nodeIndex].Topology1.w, splitFlags, originalFlags, ignored);
}

bool AllocateNodes(uint count, out uint firstNode)
{
    firstNode = 0u;
    // Counters[5] 作为无锁节点池尾指针
    [loop]
    for (uint attempt = 0u; attempt < 8u; ++attempt)
    {
        uint current;
        InterlockedAdd(Counters[5], 0u, current);
        // 容量不足时不部分提交，调用方负责回滚父节点标志
        if (current + count > NodeCapacity)
        {
            return false;
        }
        uint previous;
        // 有限重试避免高竞争下单个线程长期占用执行单元
        InterlockedCompareExchange(Counters[5], current, current + count, previous);
        if (previous == current)
        {
            firstNode = current;
            return true;
        }
    }
    return false;
}

bool ReserveSplitBudget(uint count)
{
    [loop]
    for (uint attempt = 0u; attempt < 8u; ++attempt)
    {
        uint current;
        InterlockedAdd(Counters[3], 0u, current);
        if (current < count)
        {
            InterlockedAdd(Counters[6], 1u);
            return false;
        }
        uint previous;
        InterlockedCompareExchange(Counters[3], current, current - count, previous);
        if (previous == current)
        {
            return true;
        }
    }
    InterlockedAdd(Counters[6], 1u);
    return false;
}

void ReleaseSplitBudget(uint count)
{
    InterlockedAdd(Counters[3], count);
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
    // 新节点只建立父子关系，邻接信息仍是当前 GPU-like 实现的限制
    NodeRecord child;
    child.DomainAAndB = float4(domainA, domainB);
    child.DomainCAndErrors = float4(domainC, 0.0, 0.0);
    child.Topology0 = uint4(parentIndex, InvalidNode, InvalidNode, InvalidNode);
    // 强制分裂标记用于调试着色和统计来源区分
    child.Topology1 = uint4(InvalidNode, InvalidNode, chunkId, ActiveLeafFlag | (forced ? ForcedSplitFlag : 0u));
    child.PathAndCreatedBuild = uint4(0u, 0u, BuildSequenceLow, BuildSequenceHigh);
    child.ActivatedAndSplitBuild = uint4(BuildSequenceLow, BuildSequenceHigh, 0u, 0u);
    child.MergeBuildAndDepth = uint4(0u, 0u, depth, 0u);
    Nodes[childIndex] = child;
}

void WriteSplitChildren(uint parentIndex, uint firstChild, bool forced)
{
    // ROAM 二分使用父节点底边 AB 的中点作为两个子节点公共顶点
    NodeRecord parent = Nodes[parentIndex];
    float2 a = parent.DomainAAndB.xy;
    float2 b = parent.DomainAAndB.zw;
    float2 c = parent.DomainCAndErrors.xy;
    float2 midpoint = (a + b) * 0.5;
    uint childDepth = parent.MergeBuildAndDepth.z + 1u;
    uint chunkId = parent.Topology1.z;
    // 两个子节点保持父参数域覆盖且不重叠
    WriteChildNode(firstChild, parentIndex, c, a, midpoint, childDepth, chunkId, forced);
    WriteChildNode(firstChild + 1u, parentIndex, b, c, midpoint, childDepth, chunkId, forced);
    // 最后发布父节点子索引，前序 UAV 写入由阶段 barrier 保证可见
    Nodes[parentIndex].Topology0.y = firstChild;
    Nodes[parentIndex].Topology0.z = firstChild + 1u;
    Nodes[parentIndex].ActivatedAndSplitBuild.z = BuildSequenceLow;
    Nodes[parentIndex].ActivatedAndSplitBuild.w = BuildSequenceHigh;
}

// 对候选执行一次 split 或 base-neighbor 成对 split
[numthreads(128, 1, 1)]
void CSSplitOnlyTopology(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint candidateSlot = dispatchThreadId.x;
    // 原子计数可能超过输出容量，读取时必须截断
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

    // 无 base neighbor 的边界节点可以独立分裂
    uint baseNeighbor = candidate.Topology0.w;
    if (baseNeighbor == InvalidNode)
    {
        if (!ReserveSplitBudget(1u))
        {
            return;
        }
        uint originalFlags;
        if (!MarkParentSplit(nodeIndex, originalFlags))
        {
            ReleaseSplitBudget(1u);
            return;
        }
        uint firstChild;
        // 两个子节点必须一次性分配，失败则完整回滚
        if (!AllocateNodes(2u, firstChild))
        {
            RestoreParentLeaf(nodeIndex, originalFlags);
            ReleaseSplitBudget(1u);
            return;
        }
        WriteSplitChildren(nodeIndex, firstChild, false);
        // Counters[4] 统计实际提交分裂的父节点数
        InterlockedAdd(Counters[4], 1u);
        return;
    }

    // 仅索引较小的候选负责成对提交，避免两个线程重复分裂同一菱形
    if (baseNeighbor <= nodeIndex || baseNeighbor >= NodeCapacity)
    {
        return;
    }
    NodeRecord paired = Nodes[baseNeighbor];
    // 只有互为 base neighbor 且属于同一块的活动叶才能原子成对分裂
    if (paired.Topology0.w != nodeIndex ||
        paired.Topology1.z != candidate.Topology1.z ||
        paired.MergeBuildAndDepth.z >= MaxDepth ||
        !IsActiveLeaf(baseNeighbor))
    {
        return;
    }

    if (!ReserveSplitBudget(2u))
    {
        return;
    }

    uint originalFlags;
    uint pairedFlags;
    if (!MarkParentSplit(nodeIndex, originalFlags))
    {
        ReleaseSplitBudget(2u);
        return;
    }
    // 第二个父节点竞争失败时恢复第一个父节点
    if (!MarkParentSplit(baseNeighbor, pairedFlags))
    {
        RestoreParentLeaf(nodeIndex, originalFlags);
        ReleaseSplitBudget(2u);
        return;
    }
    uint firstChild;
    // 成对分裂需要连续四个节点，容量不足时同时恢复两个父节点
    if (!AllocateNodes(4u, firstChild))
    {
        RestoreParentLeaf(baseNeighbor, pairedFlags);
        RestoreParentLeaf(nodeIndex, originalFlags);
        ReleaseSplitBudget(2u);
        return;
    }
    WriteSplitChildren(nodeIndex, firstChild, false);
    // 相邻父节点的分裂由兼容约束触发，因此标记为 forced
    WriteSplitChildren(baseNeighbor, firstChild + 2u, true);
    InterlockedAdd(Counters[4], 2u);
}

// 第二次压缩前由单线程重置活动叶计数器
[numthreads(1, 1, 1)]
void CSResetActiveLeafCount(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    Counters[0] = 0u;
}

float3 SampleNormal(float2 uv)
{
    // 使用相邻纹素中心差分估算世界空间切线
    float stepU = 1.0 / max(float(HeightMapWidth > 0u ? HeightMapWidth - 1u : 0u), 1.0);
    float stepV = 1.0 / max(float(HeightMapHeight > 0u ? HeightMapHeight - 1u : 0u), 1.0);
    float left = SampleHeight(float2(uv.x - stepU, uv.y));
    float right = SampleHeight(float2(uv.x + stepU, uv.y));
    float down = SampleHeight(float2(uv.x, uv.y - stepV));
    float up = SampleHeight(float2(uv.x, uv.y + stepV));
    float3 tangentX = float3(stepU * 2.0 * TerrainSize, (right - left) * HeightScale, 0.0);
    float3 tangentZ = float3(0.0, (up - down) * HeightScale, stepV * 2.0 * TerrainSize);
    // 叉乘顺序保持默认法向朝向正 Y
    float3 normal = cross(tangentZ, tangentX);
    return dot(normal, normal) <= 0.00000001 ? float3(0.0, 1.0, 0.0) : normalize(normal);
}

bool BuildIdMatches(uint low, uint high)
{
    // 构建序列号拆分为两个 32 位字段跨 CPU 和 GPU 传递
    return low == BuildSequenceLow && high == BuildSequenceHigh;
}

float3 LeafDebugColor(NodeRecord node)
{
    // 调试颜色区分本次普通分裂 强制分裂 历史细分和根节点
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
    // 本次构建涉及的节点使用最高亮度
    bool rebuilt = BuildIdMatches(node.ActivatedAndSplitBuild.x, node.ActivatedAndSplitBuild.y) ||
        BuildIdMatches(node.MergeBuildAndDepth.x, node.MergeBuildAndDepth.y);
    return rebuilt ? 1.0 : (node.MergeBuildAndDepth.z > 0u ? 0.70 : 0.35);
}

void WriteVertex(uint vertexIndex, float2 uv, float3 debugColor, float debugHighlight)
{
    // 写入顺序必须与 TerrainMeshVertex 和图形输入布局一致
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
    // 保持固定叶槽位到索引区间映射，同时让无效节点不产生可见面积
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
    // 拒绝损坏快照产生的非有限或越界参数坐标
    return !any(isnan(uv)) && !any(isinf(uv)) && all(uv >= 0.0) && all(uv <= 1.0);
}

// 将最终活动叶直接展开为非共享三角形和间接绘制参数
[numthreads(128, 1, 1)]
void CSMeshEmit(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint leafSlot = dispatchThreadId.x;
    uint emitLeafCount = min(Counters[0], min(ActiveLeafLimit, TriangleBudget));
    // 单个线程写入五个 DrawIndexed 参数避免额外调度
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
    // 无效节点保留退化三角形，避免改变固定输出偏移
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

    // 每个活动叶独占三个顶点，不在 GPU 上执行顶点去重
    float3 debugColor = LeafDebugColor(node);
    float debugHighlight = LeafDebugHighlight(node);
    uint vertexBase = leafSlot * 3u;
    WriteVertex(vertexBase + 0u, uv0, debugColor, debugHighlight);
    WriteVertex(vertexBase + 1u, uv1, debugColor, debugHighlight);
    WriteVertex(vertexBase + 2u, uv2, debugColor, debugHighlight);
    float3 edge0 = DomainToWorld(uv1) - DomainToWorld(uv0);
    float3 edge1 = DomainToWorld(uv2) - DomainToWorld(uv0);
    // 根据世界空间朝向修正索引顺序，保证法向和正面约定一致
    bool positiveY = cross(edge0, edge1).y >= 0.0;
    MeshIndices[vertexBase] = vertexBase;
    MeshIndices[vertexBase + 1u] = positiveY ? vertexBase + 1u : vertexBase + 2u;
    MeshIndices[vertexBase + 2u] = positiveY ? vertexBase + 2u : vertexBase + 1u;
}
