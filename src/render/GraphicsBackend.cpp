#include "render/GraphicsBackend.h"

#if defined(PARALLEL_ROAM_GRAPHICS_API_OPENGL)
#include "render/OpenGlGraphicsBackend.h"
#elif defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
#include "render/D3D12GraphicsBackend.h"
#endif

#include <memory>

namespace ParallelRoam::Render
{
std::unique_ptr<IGraphicsBackend> CreateConfiguredGraphicsBackend()
{
#if defined(PARALLEL_ROAM_GRAPHICS_API_OPENGL)
    return std::make_unique<OpenGlGraphicsBackend>();
#elif defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
    return std::make_unique<D3D12GraphicsBackend>();
#else
    return nullptr;
#endif
}

const char* ConfiguredGraphicsApiName()
{
#if defined(PARALLEL_ROAM_GRAPHICS_API_OPENGL)
    return "OpenGL";
#elif defined(PARALLEL_ROAM_GRAPHICS_API_D3D12)
    return "D3D12";
#else
    return "Unknown";
#endif
}
} // namespace ParallelRoam::Render
