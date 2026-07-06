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
    const std::array<std::uint32_t*, 8U> bufferIds{
        &NodeBufferId,
        &ActiveLeafBufferId,
        &ScreenErrorBufferId,
        &CounterBufferId,
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
    }

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

    if (TimerQueryId != 0U && glad_glDeleteQueries != nullptr)
    {
        const GLuint queryId = TimerQueryId;
        glDeleteQueries(1, &queryId);
        TimerQueryId = 0U;
    }
}
} // namespace ParallelRoam::Algorithms::GpuRoam
