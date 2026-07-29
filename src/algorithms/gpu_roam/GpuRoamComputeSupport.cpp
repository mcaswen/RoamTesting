#include "algorithms/gpu_roam/GpuRoamComputeSupport.h"

#include <glad/gl.h>

#include <algorithm>

namespace ParallelRoam::Algorithms::GpuRoam
{
namespace
{
constexpr GLuint InvalidProgramId = 0U;
// 所有内嵌 compute shader 固定使用 128 个 invocation
constexpr GLuint LocalWorkGroupSize = 128U;

// shader 编译日志在失败后仍需读取，因此删除对象前先复制到 std::string
std::string ReadShaderLog(GLuint shaderId)
{
    GLint logLength = 0;
    glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &logLength);
    // OpenGL 长度包含末尾空字符，长度一表示没有实际诊断文本
    if (logLength <= 1)
    {
        return {};
    }

    std::string log(static_cast<std::size_t>(logLength), '\0');
    GLsizei written = 0;
    // written 可能小于查询长度，缩小字符串避免输出尾部空字节
    glGetShaderInfoLog(shaderId, logLength, &written, log.data());
    log.resize(static_cast<std::size_t>(std::max(written, 0)));
    return log;
}

// program 链接日志与 shader 日志分开读取，便于区分语法和接口匹配错误
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
    // programId 同时承担缓存键，非零表示该入口已成功编译链接
    if (programId != InvalidProgramId)
    {
        return true;
    }

    // source 指向静态内嵌字符串，glShaderSource 在编译前复制其内容
    const GLuint shaderId = glCreateShader(GL_COMPUTE_SHADER);
    const GLchar* shaderSource = source;
    glShaderSource(shaderId, 1, &shaderSource, nullptr);
    glCompileShader(shaderId);

    GLint shaderCompiled = GL_FALSE;
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &shaderCompiled);
    // 编译失败不创建 program，调用方下一次仍可重试
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

    // 使用局部 nextProgramId，只有链接成功后才发布到缓存字段
    const GLuint nextProgramId = glCreateProgram();
    glAttachShader(nextProgramId, shaderId);
    glLinkProgram(nextProgramId);
    // program 链接完成后保留内部可执行代码，不再需要独立 shader 对象
    glDeleteShader(shaderId);

    GLint programLinked = GL_FALSE;
    glGetProgramiv(nextProgramId, GL_LINK_STATUS, &programLinked);
    // 链接失败必须删除局部 program，避免多次 Initialize 累积 GL 对象
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

    // 成功路径最后写入缓存，外部不会观察到半初始化 program
    programId = nextProgramId;
    return true;
}

std::uint32_t GpuRoamWorkGroupCount(std::size_t itemCount)
{
    // 空输入仍提交一个工作组，由 shader 的边界检查保持零输出
    if (itemCount == 0U)
    {
        return 1U;
    }

    // 向上取整覆盖尾部不足 128 个元素的工作组
    return static_cast<std::uint32_t>((itemCount + LocalWorkGroupSize - 1U) / LocalWorkGroupSize);
}

std::uint32_t GpuRoamLow32(std::uint64_t value)
{
    // 与 GPU buffer schema 的低高位顺序保持一致
    return static_cast<std::uint32_t>(value & 0xFFFFFFFFULL);
}

std::uint32_t GpuRoamHigh32(std::uint64_t value)
{
    return static_cast<std::uint32_t>(value >> 32U);
}

void SetGpuRoamProgramUInt(std::uint32_t programId, const char* name, std::uint32_t value)
{
    // 被优化掉的 uniform 返回 -1，静默跳过可让多个 pass 复用统一设置 helper
    const GLint location = glGetUniformLocation(programId, name);
    if (location >= 0)
    {
        glUniform1ui(location, value);
    }
}

void SetGpuRoamProgramInt(std::uint32_t programId, const char* name, int value)
{
    // sampler uniform 使用有符号整数槽位，不能复用 glUniform1ui
    const GLint location = glGetUniformLocation(programId, name);
    if (location >= 0)
    {
        glUniform1i(location, value);
    }
}

void SetGpuRoamProgramFloat(std::uint32_t programId, const char* name, float value)
{
    // 参数位置每次查询以适配不同 program，不缓存跨 program location
    const GLint location = glGetUniformLocation(programId, name);
    if (location >= 0)
    {
        glUniform1f(location, value);
    }
}

void SetGpuRoamProgramVec3(std::uint32_t programId, const char* name, const glm::vec3& value)
{
    // glm 数据不直接取地址，显式传入三个分量避免对齐配置差异
    const GLint location = glGetUniformLocation(programId, name);
    if (location >= 0)
    {
        glUniform3f(location, value.x, value.y, value.z);
    }
}

void SetGpuRoamProgramMat4(std::uint32_t programId, const char* name, const glm::mat4& value)
{
    // GLM 和 GLSL 默认均为 column-major，上传时不执行转置。
    const GLint location = glGetUniformLocation(programId, name);
    if (location >= 0)
    {
        glUniformMatrix4fv(location, 1, GL_FALSE, &value[0][0]);
    }
}

void SetGpuRoamProgramVec4Array(
    std::uint32_t programId,
    const char* name,
    const glm::vec4* values,
    std::size_t count)
{
    // 六个连续 vec4 对应 TerrainLodViewInput 的固定 frustum plane 顺序。
    const GLint location = glGetUniformLocation(programId, name);
    if (location >= 0 && values != nullptr && count > 0U)
    {
        glUniform4fv(location, static_cast<GLsizei>(count), &values[0].x);
    }
}
} // namespace ParallelRoam::Algorithms::GpuRoam
