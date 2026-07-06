#include "algorithms/gpu_roam/GpuRoamComputeSupport.h"

#include <glad/gl.h>

#include <algorithm>

namespace ParallelRoam::Algorithms::GpuRoam
{
namespace
{
constexpr GLuint InvalidProgramId = 0U;
constexpr GLuint LocalWorkGroupSize = 128U;

std::string ReadShaderLog(GLuint shaderId)
{
    GLint logLength = 0;
    glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &logLength);
    if (logLength <= 1)
    {
        return {};
    }

    std::string log(static_cast<std::size_t>(logLength), '\0');
    GLsizei written = 0;
    glGetShaderInfoLog(shaderId, logLength, &written, log.data());
    log.resize(static_cast<std::size_t>(std::max(written, 0)));
    return log;
}

std::string ReadProgramLog(GLuint programId)
{
    GLint logLength = 0;
    glGetProgramiv(programId, GL_INFO_LOG_LENGTH, &logLength);
    if (logLength <= 1)
    {
        return {};
    }

    std::string log(static_cast<std::size_t>(logLength), '\0');
    GLsizei written = 0;
    glGetProgramInfoLog(programId, logLength, &written, log.data());
    log.resize(static_cast<std::size_t>(std::max(written, 0)));
    return log;
}
} // namespace

bool EnsureGpuRoamComputeProgram(
    std::uint32_t& programId,
    const char* source,
    std::string_view label,
    std::string* errorMessage)
{
    if (programId != InvalidProgramId)
    {
        return true;
    }

    const GLuint shaderId = glCreateShader(GL_COMPUTE_SHADER);
    const GLchar* shaderSource = source;
    glShaderSource(shaderId, 1, &shaderSource, nullptr);
    glCompileShader(shaderId);

    GLint shaderCompiled = GL_FALSE;
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &shaderCompiled);
    if (shaderCompiled != GL_TRUE)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "GPU ROAM-like compute shader compile failed (" +
                            std::string{label} + "):\n" + ReadShaderLog(shaderId);
        }
        glDeleteShader(shaderId);
        return false;
    }

    const GLuint nextProgramId = glCreateProgram();
    glAttachShader(nextProgramId, shaderId);
    glLinkProgram(nextProgramId);
    glDeleteShader(shaderId);

    GLint programLinked = GL_FALSE;
    glGetProgramiv(nextProgramId, GL_LINK_STATUS, &programLinked);
    if (programLinked != GL_TRUE)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "GPU ROAM-like compute program link failed (" +
                            std::string{label} + "):\n" + ReadProgramLog(nextProgramId);
        }
        glDeleteProgram(nextProgramId);
        return false;
    }

    programId = nextProgramId;
    return true;
}

std::uint32_t GpuRoamWorkGroupCount(std::size_t itemCount)
{
    if (itemCount == 0U)
    {
        return 1U;
    }

    return static_cast<std::uint32_t>((itemCount + LocalWorkGroupSize - 1U) / LocalWorkGroupSize);
}

std::uint32_t GpuRoamLow32(std::uint64_t value)
{
    return static_cast<std::uint32_t>(value & 0xFFFFFFFFULL);
}

std::uint32_t GpuRoamHigh32(std::uint64_t value)
{
    return static_cast<std::uint32_t>(value >> 32U);
}

void SetGpuRoamProgramUInt(std::uint32_t programId, const char* name, std::uint32_t value)
{
    const GLint location = glGetUniformLocation(programId, name);
    if (location >= 0)
    {
        glUniform1ui(location, value);
    }
}

void SetGpuRoamProgramInt(std::uint32_t programId, const char* name, int value)
{
    const GLint location = glGetUniformLocation(programId, name);
    if (location >= 0)
    {
        glUniform1i(location, value);
    }
}

void SetGpuRoamProgramFloat(std::uint32_t programId, const char* name, float value)
{
    const GLint location = glGetUniformLocation(programId, name);
    if (location >= 0)
    {
        glUniform1f(location, value);
    }
}

void SetGpuRoamProgramVec3(std::uint32_t programId, const char* name, const glm::vec3& value)
{
    const GLint location = glGetUniformLocation(programId, name);
    if (location >= 0)
    {
        glUniform3f(location, value.x, value.y, value.z);
    }
}
} // namespace ParallelRoam::Algorithms::GpuRoam
