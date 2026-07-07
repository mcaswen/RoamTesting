#include "algorithms/gpu_roam/GpuRoamState.h"

#include <glad/gl.h>

#include <array>

namespace ParallelRoam::Algorithms::GpuRoam
{
static_assert(sizeof(GpuRoamDrawElementsIndirectCommand) == 5U * sizeof(std::uint32_t));

GpuRoamState::~GpuRoamState()
{
    Reset();
}

void GpuRoamState::Reset()
{
    const std::array<std::uint32_t*, 7U> bufferIds{
        &NodeBufferId,
        &ActiveLeafBufferId,
        &ScreenErrorBufferId,
        &SplitCandidateBufferId,
        &MergeCandidateBufferId,
        &GpuVertexBufferId,
        &GpuIndexBufferId,
    };
    if (glad_glDeleteBuffers != nullptr)
    {
        for (std::uint32_t* bufferId : bufferIds)
        {
            if (*bufferId != 0U)
            {
                const GLuint glBufferId = *bufferId;
                glDeleteBuffers(1, &glBufferId);
                *bufferId = 0U;
            }
        }

        if (IndirectDrawBufferId != 0U)
        {
            const GLuint glBufferId = IndirectDrawBufferId;
            glDeleteBuffers(1, &glBufferId);
            IndirectDrawBufferId = 0U;
        }

        for (GpuRoamTimingReadbackSlot& slot : TimingReadbackSlots)
        {
            if (slot.CounterBufferId != 0U)
            {
                const GLuint glBufferId = slot.CounterBufferId;
                glDeleteBuffers(1, &glBufferId);
                slot.CounterBufferId = 0U;
            }
        }
    }
    CounterBufferId = 0U;

    if (HeightMapTextureId != 0U && glad_glDeleteTextures != nullptr)
    {
        const GLuint textureId = HeightMapTextureId;
        glDeleteTextures(1, &textureId);
        HeightMapTextureId = 0U;
    }

    const std::array<std::uint32_t*, 5U> programIds{
        &ActiveLeafCompactionProgramId,
        &ErrorEvaluationProgramId,
        &CandidateMarkingProgramId,
        &MeshEmitProgramId,
        &SplitOnlyTopologyProgramId,
    };
    if (glad_glDeleteProgram != nullptr)
    {
        for (std::uint32_t* programId : programIds)
        {
            if (*programId != 0U)
            {
                glDeleteProgram(*programId);
                *programId = 0U;
            }
        }
    }

    if (glad_glDeleteQueries != nullptr)
    {
        for (GpuRoamTimingReadbackSlot& slot : TimingReadbackSlots)
        {
            if (slot.TimerQueryId != 0U)
            {
                const GLuint queryId = slot.TimerQueryId;
                glDeleteQueries(1, &queryId);
                slot.TimerQueryId = 0U;
            }
        }
    }

    NodeBufferCapacityBytes = 0U;
    ActiveLeafBufferCapacityBytes = 0U;
    ScreenErrorBufferCapacityBytes = 0U;
    SplitCandidateBufferCapacityBytes = 0U;
    MergeCandidateBufferCapacityBytes = 0U;
    GpuVertexBufferCapacityBytes = 0U;
    GpuIndexBufferCapacityBytes = 0U;
    IndirectDrawBufferCapacityBytes = 0U;
    CachedHeightMapPath.clear();
    CachedHeightMapWidth = 0;
    CachedHeightMapHeight = 0;
    HeightMapTextureUploaded = false;
    TimingReadbackCursor = 0U;
    LastCompletedCounters = {};
    LastCompletedGpuComputeMilliseconds = 0.0F;
    HasCompletedTimingReadback = false;
    for (GpuRoamTimingReadbackSlot& slot : TimingReadbackSlots)
    {
        slot = {};
    }
}
} // namespace ParallelRoam::Algorithms::GpuRoam
