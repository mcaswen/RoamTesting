#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <string>

namespace ParallelRoam::Render
{
/// <summary>
/// OpenGL shader program 的小型 RAII 封装
/// </summary>
class Shader
{
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    /// <summary>
    /// 编译 vertex shader 和 fragment shader，并链接成 program
    /// </summary>
    /// <param name="vertexSource">vertex shader 源码。</param>
    /// <param name="fragmentSource">fragment shader 源码。</param>
    /// <param name="errorMessage">失败时写入编译或链接日志。</param>
    bool Load(const char* vertexSource, const char* fragmentSource, std::string* errorMessage);

    /// <summary>
    /// 释放当前 OpenGL program
    /// </summary>
    void Destroy();

    /// <summary>
    /// 绑定当前 shader program
    /// </summary>
    void Use() const;

    /// <summary>
    /// 设置 mat4 uniform
    /// </summary>
    /// <param name="uniformName">uniform 名称。</param>
    /// <param name="value">矩阵值。</param>
    void SetMat4(const char* uniformName, const glm::mat4& value) const;

    /// <summary>
    /// 设置 vec3 uniform
    /// </summary>
    /// <param name="uniformName">uniform 名称。</param>
    /// <param name="value">向量值。</param>
    void SetVec3(const char* uniformName, const glm::vec3& value) const;

    /// <summary>
    /// 设置 float uniform
    /// </summary>
    /// <param name="uniformName">uniform 名称。</param>
    /// <param name="value">浮点值。</param>
    void SetFloat(const char* uniformName, float value) const;

    /// <summary>
    /// 设置 int uniform
    /// </summary>
    /// <param name="uniformName">uniform 名称。</param>
    /// <param name="value">整数值。</param>
    void SetInt(const char* uniformName, int value) const;

    /// <summary>
    /// 返回 OpenGL program id
    /// </summary>
    [[nodiscard]] unsigned int ProgramId() const;

private:
    // shader 编译失败时保留 OpenGL info log，避免只得到一个失败码
    static unsigned int CompileShader(unsigned int shaderType, const char* source, std::string* errorMessage);
    static std::string ReadShaderLog(unsigned int shaderId);
    static std::string ReadProgramLog(unsigned int programId);

    unsigned int _programId{0};
};
} // 命名空间 ParallelRoam::Render
