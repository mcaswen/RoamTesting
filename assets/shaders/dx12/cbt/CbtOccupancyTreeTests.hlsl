#include "CbtOccupancyTree.hlsli"

struct CbtBitUpdate
{
    uint BitIndex;
    uint Occupied;
};

cbuffer CbtTestConstants : register(b0)
{
    uint UpdateOffset;
    uint UpdateCount;
    uint DecodeCount;
    uint ResultOffset;
};

StructuredBuffer<CbtBitUpdate> CbtUpdates : register(t0);
RWStructuredBuffer<uint> CbtResults : register(u2);

groupshared uint CbtSubtree[255];
groupshared uint CbtTopTree[127];

[numthreads(64, 1, 1)]
void CSClear(uint dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId < CbtTreeSlotCount)
    {
        CbtTree[dispatchThreadId] = 0;
    }
    if (dispatchThreadId < CbtBitfieldSlotCount)
    {
        CbtBitfield[dispatchThreadId] = 0;
    }
}

[numthreads(64, 1, 1)]
void CSApplyUpdates(uint dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId >= UpdateCount)
    {
        return;
    }

    const CbtBitUpdate update = CbtUpdates[UpdateOffset + dispatchThreadId];
    if (update.BitIndex < CbtElementCount)
    {
        CbtSetBitAtomic(update.BitIndex, update.Occupied != 0);
    }
}

[numthreads(64, 1, 1)]
void CSReducePre(uint dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId >= CbtLastTreeNodeCount)
    {
        return;
    }

    // 每个最深计数节点覆盖两个连续 64 位位域槽位
    const uint bitfieldIndex = dispatchThreadId * 2;
    const uint count = countbits(CbtBitfield[bitfieldIndex]) + countbits(CbtBitfield[bitfieldIndex + 1]);
    CbtWriteTreeCountAtomic((1u << CbtLastTreeDepth) + dispatchThreadId, count);
}

[numthreads(64, 1, 1)]
void CSReduceFirst(uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    const uint subtreeIndex = groupId.x;
    if (subtreeIndex >= CbtSubtreeCount)
    {
        return;
    }

    // 一个工作组负责 16K 位，对应 128 个最深压缩计数
    const uint blockBase = subtreeIndex * 128;
    for (uint element = 0; element < 2; ++element)
    {
        const uint localLeaf = groupIndex + element * 64;
        CbtSubtree[127 + localLeaf] =
            CbtReadTreeCount((1u << CbtLastTreeDepth) + blockBase + localLeaf);
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint width = 64; width > 0; width >>= 1)
    {
        if (groupIndex < width)
        {
            const uint parentStart = width - 1;
            const uint childStart = width * 2 - 1;
            CbtSubtree[parentStart + groupIndex] =
                CbtSubtree[childStart + groupIndex * 2] +
                CbtSubtree[childStart + groupIndex * 2 + 1];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // 最深层已由预归约写入，这里发布子树内部的七层计数
    for (uint localIndex = groupIndex; localIndex < 127; localIndex += 64)
    {
        const uint localHeapId = localIndex + 1;
        const uint localDepth = uint(firstbithigh(localHeapId));
        const uint localElement = localHeapId - (1u << localDepth);
        const uint globalDepth = CbtSubtreeRootDepth + localDepth;
        const uint globalElement = subtreeIndex * (1u << localDepth) + localElement;
        CbtWriteTreeCountAtomic((1u << globalDepth) + globalElement, CbtSubtree[localIndex]);
    }
}

[numthreads(64, 1, 1)]
void CSReduceSecond(uint groupIndex : SV_GroupIndex)
{
    const uint leafStart = CbtSubtreeCount - 1;
    if (groupIndex < CbtSubtreeCount)
    {
        CbtTopTree[leafStart + groupIndex] =
            CbtReadTreeCount((1u << CbtSubtreeRootDepth) + groupIndex);
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint width = CbtSubtreeCount / 2; width > 0; width >>= 1)
    {
        if (groupIndex < width)
        {
            const uint parentStart = width - 1;
            const uint childStart = width * 2 - 1;
            CbtTopTree[parentStart + groupIndex] =
                CbtTopTree[childStart + groupIndex * 2] +
                CbtTopTree[childStart + groupIndex * 2 + 1];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    for (uint localIndex = groupIndex; localIndex < leafStart; localIndex += 64)
    {
        CbtWriteTreeCountAtomic(localIndex + 1, CbtTopTree[localIndex]);
    }
    DeviceMemoryBarrierWithGroupSync();

    if (groupIndex == 0)
    {
        CbtResults[0] = CbtTopTree[0];
    }
}

[numthreads(64, 1, 1)]
void CSDecodeOccupied(uint dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId < DecodeCount)
    {
        CbtResults[ResultOffset + dispatchThreadId] = CbtDecodeBit(dispatchThreadId);
    }
}

[numthreads(64, 1, 1)]
void CSDecodeFree(uint dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId < DecodeCount)
    {
        CbtResults[ResultOffset + dispatchThreadId] = CbtDecodeBitComplement(dispatchThreadId);
    }
}
