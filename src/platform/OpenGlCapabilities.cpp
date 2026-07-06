#include "platform/OpenGlCapabilities.h"

#include <glad/gl.h>

#include <sstream>

namespace ParallelRoam::Platform
{
namespace
{
std::string ReadGlString(GLenum name)
{
    if (glad_glGetString == nullptr)
    {
        return {};
    }

    const GLubyte* value = glGetString(name);
    if (value == nullptr)
    {
        return {};
    }

    return reinterpret_cast<const char*>(value);
}
} // namespace

bool OpenGlGpuCapabilities::SupportsGpuRoamCompute() const
{
    return HasActiveContext &&
           SupportsOpenGl43 &&
           SupportsComputeShader &&
           SupportsShaderStorageBufferObject &&
           SupportsAtomicCounters &&
           SupportsTimerQuery;
}

std::string OpenGlGpuCapabilities::GpuRoamComputeUnavailableReason() const
{
    if (!HasActiveContext)
    {
        return "no active OpenGL context or GLAD loader is not initialized";
    }

    if (!SupportsOpenGl43)
    {
        std::ostringstream stream;
        stream << "OpenGL 4.3 is required for compute shader ROAM, current context is "
               << MajorVersion << '.' << MinorVersion;
        if (!VersionString.empty())
        {
            stream << " (" << VersionString << ')';
        }
        return stream.str();
    }

    if (!SupportsComputeShader)
    {
        return "GL_ARB_compute_shader is unavailable";
    }

    if (!SupportsShaderStorageBufferObject)
    {
        return "GL_ARB_shader_storage_buffer_object is unavailable";
    }

    if (!SupportsAtomicCounters)
    {
        return "GL_ARB_shader_atomic_counters is unavailable";
    }

    if (!SupportsTimerQuery)
    {
        return "GL_ARB_timer_query is unavailable, GPU benchmark timing would be ambiguous";
    }

    return {};
}

OpenGlGpuCapabilities QueryOpenGlGpuCapabilities()
{
    OpenGlGpuCapabilities capabilities{};
    if (glad_glGetString == nullptr || glad_glGetIntegerv == nullptr)
    {
        return capabilities;
    }

    capabilities.VersionString = ReadGlString(GL_VERSION);
    capabilities.RendererString = ReadGlString(GL_RENDERER);
    if (capabilities.VersionString.empty())
    {
        return capabilities;
    }

    capabilities.HasActiveContext = true;
    GLint majorVersion = 0;
    GLint minorVersion = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
    glGetIntegerv(GL_MINOR_VERSION, &minorVersion);
    capabilities.MajorVersion = static_cast<int>(majorVersion);
    capabilities.MinorVersion = static_cast<int>(minorVersion);

    capabilities.SupportsOpenGl43 =
        (capabilities.MajorVersion > 4 || (capabilities.MajorVersion == 4 && capabilities.MinorVersion >= 3)) ||
        GLAD_GL_VERSION_4_3 != 0;
    capabilities.SupportsComputeShader = capabilities.SupportsOpenGl43 || GLAD_GL_ARB_compute_shader != 0;
    capabilities.SupportsShaderStorageBufferObject =
        capabilities.SupportsOpenGl43 || GLAD_GL_ARB_shader_storage_buffer_object != 0;
    capabilities.SupportsAtomicCounters =
        capabilities.SupportsOpenGl43 || GLAD_GL_ARB_shader_atomic_counters != 0;
    capabilities.SupportsIndirectDraw = capabilities.SupportsOpenGl43 || GLAD_GL_ARB_draw_indirect != 0;
    capabilities.SupportsTimerQuery = capabilities.SupportsOpenGl43 || GLAD_GL_ARB_timer_query != 0;
    return capabilities;
}
} // namespace ParallelRoam::Platform
