#include "CbtOccupancyTree.hlsli"

RWStructuredBuffer<uint64_t> CbtHeapIds : register(u2);
RWStructuredBuffer<uint3> CbtCurrentNeighbors : register(u3);
RWStructuredBuffer<uint3> CbtNextNeighbors : register(u4);
RWStructuredBuffer<uint> CbtBisectorDataWords : register(u5);
RWStructuredBuffer<uint> CbtClassification : register(u6);
RWStructuredBuffer<uint> CbtSimplification : register(u7);
RWStructuredBuffer<int> CbtAllocation : register(u8);
RWStructuredBuffer<int> CbtPropagation : register(u9);
RWStructuredBuffer<int> CbtMemory : register(u10);
RWStructuredBuffer<uint> CbtTopologyIndirect : register(u11);
RWStructuredBuffer<uint> CbtDrawState : register(u12);
RWStructuredBuffer<uint> CbtActiveIndices : register(u13);
RWStructuredBuffer<uint> CbtVisibleIndices : register(u14);
RWStructuredBuffer<uint> CbtModifiedIndices : register(u15);
RWStructuredBuffer<uint> CbtValidation : register(u16);

[numthreads(1, 1, 1)]
void CSResetE0(uint dispatchThreadId : SV_DispatchThreadID)
{
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

    CbtDrawState[0] = 0;
    CbtDrawState[1] = 1;
    CbtDrawState[2] = 0;
    CbtDrawState[3] = 0;
    CbtDrawState[4] = 0;
    CbtDrawState[5] = 1;
    CbtDrawState[6] = 0;
    CbtDrawState[7] = 0;
    CbtDrawState[8] = 0;
    CbtDrawState[9] = 0;
}

[numthreads(64, 1, 1)]
void CSReducePre(uint dispatchThreadId : SV_DispatchThreadID)
{
    CbtReducePre(dispatchThreadId);
}

[numthreads(64, 1, 1)]
void CSReduceFirst(uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    CbtReduceFirst(groupId, groupIndex);
}

[numthreads(64, 1, 1)]
void CSReduceSecond(uint groupIndex : SV_GroupIndex)
{
    CbtReduceSecond(groupIndex);
}
