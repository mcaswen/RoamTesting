#include "algorithms/gpu_roam/GpuRoamTerrainLodAlgorithm.h"

#include "platform/OpenGlCapabilities.h"

namespace ParallelRoam::Algorithms::GpuRoam
{
namespace
{
std::string BuildAdapterOnlyReason()
{
    return "GPU ROAM-like Level A adapter is registered, but compute and GPU render passes are not implemented yet";
}
} // namespace

TerrainLodAlgorithmInfo GpuRoamTerrainLodAlgorithm::Info() const
{
    return TerrainLodAlgorithmInfo{
        TerrainLodAlgorithmId::GpuRoamLike,
        "gpu_roam_like",
        "GPU ROAM-like",
        "GPU-oriented ROAM pipeline adapter with capability gate",
    };
}

TerrainLodAlgorithmCapabilities GpuRoamTerrainLodAlgorithm::Capabilities() const
{
    TerrainLodAlgorithmCapabilities capabilities{};
    // 4A 只接入算法壳和硬件能力门禁，真正 GPU 输出在后续阶段打开
    capabilities.SupportsCpuMeshOutput = false;
    capabilities.SupportsGpuDrivenRendering = false;
    capabilities.SupportsSplit = false;
    capabilities.SupportsMerge = false;
    capabilities.SupportsCrackFix = false;
    capabilities.SupportsTopologyValidation = false;
    return capabilities;
}

bool GpuRoamTerrainLodAlgorithm::BuildRenderData(
    const TerrainLodBuildInput& input,
    TerrainLodRenderPacket& outPacket,
    std::string* errorMessage)
{
    _stats = {};
    outPacket = {};
    outPacket.Mode = TerrainLodRenderMode::DebugOnly;

    if (input.HeightMap == nullptr || !input.HeightMap->IsValid())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "GPU ROAM-like build failed: invalid height map";
        }
        return false;
    }

    const std::string unavailableReason = GpuRoamLikeUnavailableReason();
    if (!unavailableReason.empty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "GPU ROAM-like unavailable: " + unavailableReason;
        }
        return false;
    }

    if (errorMessage != nullptr)
    {
        *errorMessage = BuildAdapterOnlyReason();
    }
    return false;
}

const TerrainLodStats& GpuRoamTerrainLodAlgorithm::Stats() const
{
    return _stats;
}

void GpuRoamTerrainLodAlgorithm::Reset()
{
    _stats = {};
}

std::string GpuRoamLikeUnavailableReason()
{
    const Platform::OpenGlGpuCapabilities capabilities = Platform::QueryOpenGlGpuCapabilities();
    const std::string capabilityReason = capabilities.GpuRoamComputeUnavailableReason();
    if (!capabilityReason.empty())
    {
        return capabilityReason;
    }

    return BuildAdapterOnlyReason();
}
} // namespace ParallelRoam::Algorithms::GpuRoam
