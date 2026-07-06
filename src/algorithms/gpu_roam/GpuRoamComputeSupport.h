#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ParallelRoam::Algorithms::GpuRoam
{
[[nodiscard]] bool EnsureGpuRoamComputeProgram(
    std::uint32_t& programId,
    const char* source,
    std::string_view label,
    std::string* errorMessage);

[[nodiscard]] std::uint32_t GpuRoamWorkGroupCount(std::size_t itemCount);
[[nodiscard]] std::uint32_t GpuRoamLow32(std::uint64_t value);
[[nodiscard]] std::uint32_t GpuRoamHigh32(std::uint64_t value);

void SetGpuRoamProgramUInt(std::uint32_t programId, const char* name, std::uint32_t value);
void SetGpuRoamProgramInt(std::uint32_t programId, const char* name, int value);
void SetGpuRoamProgramFloat(std::uint32_t programId, const char* name, float value);
void SetGpuRoamProgramVec3(std::uint32_t programId, const char* name, const glm::vec3& value);
} // namespace ParallelRoam::Algorithms::GpuRoam
