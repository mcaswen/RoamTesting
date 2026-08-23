#pragma once

#include "algorithms/cbt_2024/CbtGpuAbi.shared.h"

#include <cstdint>

namespace ParallelRoam::Algorithms::Cbt2024
{
// C++ 侧只提供强类型别名；数值唯一来源是 DXC 同时包含的 shared header。
// GPU buffer 布局变化必须先修改共享 word index，再由 offsetof 断言验证结构体。
inline constexpr std::uint32_t InvalidCbtBisectorIndex = CBT_GPU_INVALID_INDEX;
inline constexpr std::uint32_t CbtBaseBisectorCount = CBT_GPU_BASE_BISECTOR_COUNT;
inline constexpr std::uint32_t CbtBaseControlPointCount = CbtBaseBisectorCount * 3U;

inline constexpr std::uint32_t CbtVisibleFlag = CBT_GPU_VISIBLE_FLAG;
// Flags 和 element state 分属不同字段，保持独立常量避免位值/枚举值误用。
inline constexpr std::uint32_t CbtModifiedFlag = CBT_GPU_MODIFIED_FLAG;
inline constexpr std::uint32_t CbtUnchangedElement = CBT_GPU_UNCHANGED_ELEMENT;
inline constexpr std::uint32_t CbtBisectElement = CBT_GPU_BISECT_ELEMENT;
inline constexpr std::uint32_t CbtSimplifyElement = CBT_GPU_SIMPLIFY_ELEMENT;
inline constexpr std::uint32_t CbtMergedElement = CBT_GPU_MERGED_ELEMENT;

inline constexpr std::uint32_t CbtNoSplitPattern = CBT_GPU_NO_SPLIT_PATTERN;
// pattern 是可组合位图；double/triple 值只由三个基础模板位派生。
inline constexpr std::uint32_t CbtCenterSplitPattern = CBT_GPU_CENTER_SPLIT_PATTERN;
inline constexpr std::uint32_t CbtRightSplitPattern = CBT_GPU_RIGHT_SPLIT_PATTERN;
inline constexpr std::uint32_t CbtLeftSplitPattern = CBT_GPU_LEFT_SPLIT_PATTERN;
inline constexpr std::uint32_t CbtRightDoubleSplitPattern =
    CbtCenterSplitPattern | CbtRightSplitPattern;
inline constexpr std::uint32_t CbtLeftDoubleSplitPattern =
    CbtCenterSplitPattern | CbtLeftSplitPattern;
inline constexpr std::uint32_t CbtTripleSplitPattern =
    CbtCenterSplitPattern | CbtRightSplitPattern | CbtLeftSplitPattern;

inline constexpr std::uint32_t CbtBisectorDataWordCount = CBT_GPU_BISECTOR_DATA_WORD_COUNT;
// word count 用于资源容量、readback 范围和跨语言静态布局检查。
inline constexpr std::uint32_t CbtDrawStateWordCount = CBT_GPU_DRAW_STATE_WORD_COUNT;
inline constexpr std::uint32_t CbtIndirectDispatchWordCount = CBT_GPU_GEOMETRY_DISPATCH_WORD_COUNT;
inline constexpr std::uint32_t CbtValidationWordCount = CBT_GPU_VALIDATION_WORD_COUNT;
} // namespace ParallelRoam::Algorithms::Cbt2024
