#include "algorithms/cbt_2024/CbtOccupancyTree.h"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace ParallelRoam::Algorithms::Cbt2024
{
namespace
{
constexpr std::uint32_t FullWidthDepthCount = 7U;
constexpr std::uint32_t BitfieldBlockSize = 128U; // 最深 8 位计数的覆盖范围
constexpr std::uint32_t ReductionSubtreeSize = 16384U; // 单个中段归约工作组的位范围

std::uint32_t CapacityValue(CbtOccupancyCapacity capacity)
{
    return static_cast<std::uint32_t>(capacity);
}

std::uint32_t ComputeTreeDepthOffsetBits(std::uint32_t depth)
{
    if (depth <= FullWidthDepthCount)
    {
        return 32U * ((1U << depth) - 1U);
    }

    // 深度 0 到 6 使用 32 位，后续非叶计数使用 16 位
    return 32U * ((1U << FullWidthDepthCount) - 1U) +
           16U * ((1U << depth) - (1U << FullWidthDepthCount));
}

void AppendSetBits(
    std::uint64_t word,
    std::uint32_t baseIndex,
    std::vector<std::uint32_t>& output)
{
    while (word != 0U)
    {
        const std::uint32_t localIndex = static_cast<std::uint32_t>(std::countr_zero(word));
        output.push_back(baseIndex + localIndex);
        word &= word - 1U;
    }
}
} // namespace

CbtOccupancyLayout BuildCbtOccupancyLayout(CbtOccupancyCapacity capacity)
{
    const std::uint32_t elementCount = CapacityValue(capacity);
    if (elementCount < 2U || !std::has_single_bit(elementCount))
    {
        throw std::invalid_argument{"CBT occupancy capacity must be a supported power of two"};
    }

    CbtOccupancyLayout layout{};
    layout.Capacity = capacity; // 保存枚举值供 shader 名称和日志复用
    layout.ElementCount = elementCount; // 物理槽位与叶节点一一对应
    layout.LeafDepth = static_cast<std::uint32_t>(std::countr_zero(elementCount)); // 容量是二次幂
    layout.LastTreeDepth = layout.LeafDepth - 7U; // 最后七层由 128 位块和位域承担
    layout.LastTreeNodeCount = elementCount / BitfieldBlockSize; // 每个节点写入一个 8 位计数
    layout.BitfieldSlotCount = elementCount / 64U; // 位域使用原生 64 位原子槽位
    layout.SubtreeRootDepth = layout.LeafDepth - 14U; // 每个中段子树固定覆盖 14 层叶路径
    layout.SubtreeCount = elementCount / ReductionSubtreeSize; // 容量增大只增加独立工作组

    const std::uint32_t treeBits =
        ComputeTreeDepthOffsetBits(layout.LastTreeDepth) + 8U * layout.LastTreeNodeCount;
    layout.TreeSlotCount = treeBits / 32U; // 所有压缩层均保持 32 位槽位对齐
    return layout;
}

const char* CbtOccupancyCapacityName(CbtOccupancyCapacity capacity)
{
    switch (capacity)
    {
    case CbtOccupancyCapacity::Capacity128K:
        return "128K";
    case CbtOccupancyCapacity::Capacity256K:
        return "256K";
    case CbtOccupancyCapacity::Capacity512K:
        return "512K";
    case CbtOccupancyCapacity::Capacity1M:
        return "1M";
    }
    return "unknown";
}

CbtOccupancyTree::CbtOccupancyTree(CbtOccupancyCapacity capacity)
    : _layout(BuildCbtOccupancyLayout(capacity)),
      _packedTree(_layout.TreeSlotCount, 0U),
      _bitfield(_layout.BitfieldSlotCount, 0U)
{
}

const CbtOccupancyLayout& CbtOccupancyTree::Layout() const
{
    return _layout;
}

bool CbtOccupancyTree::SetBit(std::uint32_t bitIndex, bool occupied)
{
    if (bitIndex >= _layout.ElementCount)
    {
        return false;
    }

    const std::uint32_t slot = bitIndex / 64U; // 选择 64 位原子槽位
    const std::uint32_t localIndex = bitIndex % 64U; // 选择槽位内 bit
    const std::uint64_t mask = std::uint64_t{1U} << localIndex; // 避免 32 位移位截断
    if (occupied)
    {
        _bitfield[slot] |= mask;
    }
    else
    {
        _bitfield[slot] &= ~mask;
    }
    return true;
}

bool CbtOccupancyTree::GetBit(std::uint32_t bitIndex) const
{
    if (bitIndex >= _layout.ElementCount)
    {
        return false;
    }

    const std::uint32_t slot = bitIndex / 64U;
    const std::uint32_t localIndex = bitIndex % 64U;
    return (_bitfield[slot] & (std::uint64_t{1U} << localIndex)) != 0U;
}

void CbtOccupancyTree::Clear()
{
    std::fill(_packedTree.begin(), _packedTree.end(), 0U);
    std::fill(_bitfield.begin(), _bitfield.end(), 0U);
}

void CbtOccupancyTree::Reduce()
{
    std::fill(_packedTree.begin(), _packedTree.end(), 0U);

    // 最深压缩层的每个 8 位计数覆盖两个 64 位位域槽位
    const std::uint32_t lastLevelHeapStart = 1U << _layout.LastTreeDepth; // 一基完全二叉堆层起点
    for (std::uint32_t blockIndex = 0U; blockIndex < _layout.LastTreeNodeCount; ++blockIndex)
    {
        const std::uint32_t bitfieldIndex = blockIndex * 2U; // 相邻两个 uint64 组成 128 位块
        const std::uint32_t count =
            static_cast<std::uint32_t>(std::popcount(_bitfield[bitfieldIndex])) +
            static_cast<std::uint32_t>(std::popcount(_bitfield[bitfieldIndex + 1U]));
        SetHeapElement(lastLevelHeapStart + blockIndex, count);
    }

    // 上层计数自底向上归约，根计数最终等于全部活动位数量
    for (std::uint32_t depth = _layout.LastTreeDepth; depth-- > 0U;)
    {
        const std::uint32_t levelStart = 1U << depth; // 一基堆中该层首节点
        const std::uint32_t levelCount = 1U << depth; // 完整二叉树该层节点数
        for (std::uint32_t element = 0U; element < levelCount; ++element)
        {
            const std::uint32_t heapId = levelStart + element;
            SetHeapElement(heapId, GetHeapElement(heapId * 2U) + GetHeapElement(heapId * 2U + 1U));
        }
    }
}

std::uint32_t CbtOccupancyTree::BitCount() const
{
    return _packedTree.empty() ? 0U : _packedTree[0];
}

std::uint32_t CbtOccupancyTree::DecodeBit(std::uint32_t rank) const
{
    if (rank >= BitCount())
    {
        return InvalidCbtBitIndex;
    }

    std::uint32_t heapId = 1U; // rank-select 从根节点开始
    for (std::uint32_t depth = 0U; depth < _layout.LastTreeDepth; ++depth)
    {
        const std::uint32_t leftCount = GetHeapElement(heapId * 2U);
        const bool selectRight = rank >= leftCount; // 左子树容不下 rank 时进入右子树
        heapId = heapId * 2U + static_cast<std::uint32_t>(selectRight);
        if (selectRight)
        {
            rank -= leftCount;
        }
    }

    const std::uint32_t blockIndex = heapId - (1U << _layout.LastTreeDepth); // 转为零基 128 位块
    const std::uint32_t bitfieldIndex = blockIndex * 2U; // 块内先检查低地址 uint64
    const std::uint32_t firstWordCount = static_cast<std::uint32_t>(std::popcount(_bitfield[bitfieldIndex]));
    if (rank < firstWordCount)
    {
        return blockIndex * BitfieldBlockSize + SelectOne(_bitfield[bitfieldIndex], rank);
    }

    return blockIndex * BitfieldBlockSize + 64U +
           SelectOne(_bitfield[bitfieldIndex + 1U], rank - firstWordCount);
}

std::uint32_t CbtOccupancyTree::DecodeBitComplement(std::uint32_t rank) const
{
    const std::uint32_t freeCount = _layout.ElementCount - BitCount(); // 根计数的补集
    if (rank >= freeCount)
    {
        return InvalidCbtBitIndex;
    }

    std::uint32_t heapId = 1U; // 空闲选择与活动选择共享同一压缩树
    for (std::uint32_t depth = 0U; depth < _layout.LastTreeDepth; ++depth)
    {
        const std::uint32_t childCapacity = _layout.ElementCount >> (depth + 1U); // 当前子节点覆盖位数
        const std::uint32_t leftFreeCount = childCapacity - GetHeapElement(heapId * 2U); // 活动计数取补集
        const bool selectRight = rank >= leftFreeCount;
        heapId = heapId * 2U + static_cast<std::uint32_t>(selectRight);
        if (selectRight)
        {
            rank -= leftFreeCount;
        }
    }

    const std::uint32_t blockIndex = heapId - (1U << _layout.LastTreeDepth); // 选中的 128 位块
    const std::uint32_t bitfieldIndex = blockIndex * 2U; // 对应两个连续 uint64
    const std::uint32_t firstWordCount = static_cast<std::uint32_t>(std::popcount(~_bitfield[bitfieldIndex]));
    if (rank < firstWordCount)
    {
        return blockIndex * BitfieldBlockSize + SelectOne(~_bitfield[bitfieldIndex], rank);
    }

    return blockIndex * BitfieldBlockSize + 64U +
           SelectOne(~_bitfield[bitfieldIndex + 1U], rank - firstWordCount);
}

std::vector<std::uint32_t> CbtOccupancyTree::ActiveIndices() const
{
    std::vector<std::uint32_t> result;
    result.reserve(BitCount());
    for (std::uint32_t slot = 0U; slot < _layout.BitfieldSlotCount; ++slot)
    {
        AppendSetBits(_bitfield[slot], slot * 64U, result);
    }
    return result;
}

std::vector<std::uint32_t> CbtOccupancyTree::FreeIndices() const
{
    std::vector<std::uint32_t> result;
    result.reserve(_layout.ElementCount - BitCount());
    for (std::uint32_t slot = 0U; slot < _layout.BitfieldSlotCount; ++slot)
    {
        AppendSetBits(~_bitfield[slot], slot * 64U, result);
    }
    return result;
}

const std::vector<std::uint32_t>& CbtOccupancyTree::PackedTree() const
{
    return _packedTree;
}

const std::vector<std::uint64_t>& CbtOccupancyTree::Bitfield() const
{
    return _bitfield;
}

std::uint32_t CbtOccupancyTree::TreeElementWidth(std::uint32_t depth) const
{
    if (depth < FullWidthDepthCount)
    {
        return 32U;
    }
    return depth < _layout.LastTreeDepth ? 16U : 8U;
}

std::uint32_t CbtOccupancyTree::TreeDepthOffsetBits(std::uint32_t depth) const
{
    return ComputeTreeDepthOffsetBits(depth);
}

std::uint32_t CbtOccupancyTree::GetHeapElement(std::uint32_t heapId) const
{
    if (heapId == 0U)
    {
        return 0U;
    }

    const std::uint32_t depth = static_cast<std::uint32_t>(std::bit_width(heapId) - 1U); // 一基堆深度
    if (depth > _layout.LastTreeDepth)
    {
        return 0U;
    }

    const std::uint32_t width = TreeElementWidth(depth); // 32 位、16 位或 8 位字段
    const std::uint32_t element = heapId - (1U << depth); // 层内零基索引
    const std::uint32_t firstBit = TreeDepthOffsetBits(depth) + width * element; // 压缩流偏移
    const std::uint32_t slot = firstBit / 32U; // 目标 uint 槽位
    const std::uint32_t localIndex = firstBit % 32U; // 槽位内字段起点
    const std::uint32_t mask = width == 32U ? 0xffffffffU : ((1U << width) - 1U);
    return (_packedTree[slot] >> localIndex) & mask;
}

void CbtOccupancyTree::SetHeapElement(std::uint32_t heapId, std::uint32_t value)
{
    const std::uint32_t depth = static_cast<std::uint32_t>(std::bit_width(heapId) - 1U); // 一基堆深度
    const std::uint32_t width = TreeElementWidth(depth); // 当前层计数字段宽度
    const std::uint32_t element = heapId - (1U << depth); // 当前层零基索引
    const std::uint32_t firstBit = TreeDepthOffsetBits(depth) + width * element; // 压缩流位置
    const std::uint32_t slot = firstBit / 32U; // 写入目标 uint
    const std::uint32_t localIndex = firstBit % 32U; // 写入字段偏移
    const std::uint32_t mask = width == 32U ? 0xffffffffU : ((1U << width) - 1U);
    _packedTree[slot] &= ~(mask << localIndex);
    _packedTree[slot] |= (value & mask) << localIndex;
}

std::uint32_t CbtOccupancyTree::SelectOne(std::uint64_t word, std::uint32_t rank) const
{
    while (word != 0U)
    {
        const std::uint32_t localIndex = static_cast<std::uint32_t>(std::countr_zero(word));
        if (rank == 0U)
        {
            return localIndex;
        }
        --rank;
        word &= word - 1U;
    }
    return InvalidCbtBitIndex;
}
} // namespace ParallelRoam::Algorithms::Cbt2024
