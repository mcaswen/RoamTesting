#include "CbtOccupancyTree.hlsli"

// u0 和 u1 由 OCBT include 声明 其余寄存器严格延续上游 topology ABI
RWStructuredBuffer<uint64_t> CbtHeapIds : register(u2);
RWStructuredBuffer<uint3> CbtCurrentNeighbors : register(u3);
RWStructuredBuffer<uint3> CbtNextNeighbors : register(u4);
RWStructuredBuffer<uint> CbtBisectorDataWords : register(u5);
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
    CbtDrawState[9] = 0;
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
