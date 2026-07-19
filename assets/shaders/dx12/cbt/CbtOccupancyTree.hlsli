#ifndef PARALLEL_ROAM_CBT_OCCUPANCY_TREE_HLSLI
#define PARALLEL_ROAM_CBT_OCCUPANCY_TREE_HLSLI

#ifndef CBT_CAPACITY
#error CBT_CAPACITY must select an OCBT capacity specialization
#endif

#if CBT_CAPACITY == 131072
static const uint CbtLeafDepth = 17;
static const uint CbtLastTreeDepth = 10;
static const uint CbtTreeSlotCount = 831;
#elif CBT_CAPACITY == 262144
static const uint CbtLeafDepth = 18;
static const uint CbtLastTreeDepth = 11;
static const uint CbtTreeSlotCount = 1599;
#elif CBT_CAPACITY == 524288
static const uint CbtLeafDepth = 19;
static const uint CbtLastTreeDepth = 12;
static const uint CbtTreeSlotCount = 3135;
#elif CBT_CAPACITY == 1048576
static const uint CbtLeafDepth = 20;
static const uint CbtLastTreeDepth = 13;
static const uint CbtTreeSlotCount = 6207;
#else
#error Unsupported CBT_CAPACITY
#endif

static const uint CbtElementCount = CBT_CAPACITY;
static const uint CbtBitfieldSlotCount = CbtElementCount / 64;
static const uint CbtLastTreeNodeCount = CbtElementCount / 128;
static const uint CbtSubtreeRootDepth = CbtLeafDepth - 14;
static const uint CbtSubtreeCount = CbtElementCount / 16384;

RWStructuredBuffer<uint> CbtTree : register(u0);
RWStructuredBuffer<uint64_t> CbtBitfield : register(u1);

uint CbtTreeElementWidth(uint depth)
{
    if (depth < 7)
    {
        return 32;
    }
    return depth < CbtLastTreeDepth ? 16 : 8;
}

uint CbtTreeDepthOffsetBits(uint depth)
{
    if (depth <= 7)
    {
        return 32 * ((1u << depth) - 1u);
    }

    // 前七层保留 32 位计数，后续非叶层使用 16 位计数
    return 32 * 127 + 16 * ((1u << depth) - 128u);
}

uint CbtReadTreeCount(uint heapId)
{
    const uint depth = uint(firstbithigh(heapId));
    const uint width = CbtTreeElementWidth(depth);
    const uint element = heapId - (1u << depth);
    const uint firstBit = CbtTreeDepthOffsetBits(depth) + width * element;
    const uint slot = firstBit / 32;
    const uint localBit = firstBit % 32;
    const uint mask = width == 32 ? 0xffffffffu : ((1u << width) - 1u);
    return (CbtTree[slot] >> localBit) & mask;
}

void CbtWriteTreeCountAtomic(uint heapId, uint value)
{
    const uint depth = uint(firstbithigh(heapId));
    const uint width = CbtTreeElementWidth(depth);
    const uint element = heapId - (1u << depth);
    const uint firstBit = CbtTreeDepthOffsetBits(depth) + width * element;
    const uint slot = firstBit / 32;
    const uint localBit = firstBit % 32;
    if (width == 32)
    {
        // 32 位层的节点独占完整槽位，归约调度保证单写者
        CbtTree[slot] = value;
        return;
    }

    // 同一 32 位槽位中的字段互不重叠，原子清位和置位可并发提交
    const uint valueMask = (1u << width) - 1u;
    const uint fieldMask = valueMask << localBit;
    InterlockedAnd(CbtTree[slot], ~fieldMask);
    InterlockedOr(CbtTree[slot], (value & valueMask) << localBit);
}

void CbtSetBitAtomic(uint bitIndex, bool occupied)
{
    const uint slot = bitIndex / 64;
    const uint localBit = bitIndex % 64;
    const uint64_t mask = uint64_t(1) << localBit;
    if (occupied)
    {
        InterlockedOr(CbtBitfield[slot], mask);
    }
    else
    {
        InterlockedAnd(CbtBitfield[slot], ~mask);
    }
}

uint CbtSelectOne(uint64_t word, uint rank)
{
    [loop]
    for (uint bit = 0; bit < 64; ++bit)
    {
        if ((word & (uint64_t(1) << bit)) != 0)
        {
            if (rank == 0)
            {
                return bit;
            }
            --rank;
        }
    }
    return 0xffffffffu;
}

uint CbtDecodeBit(uint rank)
{
    uint heapId = 1;
    for (uint depth = 0; depth < CbtLastTreeDepth; ++depth)
    {
        const uint leftCount = CbtReadTreeCount(heapId * 2);
        const uint selectRight = rank >= leftCount ? 1 : 0;
        heapId = heapId * 2 + selectRight;
        rank -= leftCount * selectRight;
    }

    const uint blockIndex = heapId - (1u << CbtLastTreeDepth);
    const uint bitfieldIndex = blockIndex * 2;
    const uint firstWordCount = countbits(CbtBitfield[bitfieldIndex]);
    if (rank < firstWordCount)
    {
        return blockIndex * 128 + CbtSelectOne(CbtBitfield[bitfieldIndex], rank);
    }
    return blockIndex * 128 + 64 + CbtSelectOne(CbtBitfield[bitfieldIndex + 1], rank - firstWordCount);
}

uint CbtDecodeBitComplement(uint rank)
{
    uint heapId = 1;
    for (uint depth = 0; depth < CbtLastTreeDepth; ++depth)
    {
        const uint childCapacity = CbtElementCount >> (depth + 1);
        const uint leftFreeCount = childCapacity - CbtReadTreeCount(heapId * 2);
        const uint selectRight = rank >= leftFreeCount ? 1 : 0;
        heapId = heapId * 2 + selectRight;
        rank -= leftFreeCount * selectRight;
    }

    const uint blockIndex = heapId - (1u << CbtLastTreeDepth);
    const uint bitfieldIndex = blockIndex * 2;
    const uint64_t firstWord = ~CbtBitfield[bitfieldIndex];
    const uint firstWordCount = countbits(firstWord);
    if (rank < firstWordCount)
    {
        return blockIndex * 128 + CbtSelectOne(firstWord, rank);
    }
    return blockIndex * 128 + 64 + CbtSelectOne(~CbtBitfield[bitfieldIndex + 1], rank - firstWordCount);
}

#endif
