#pragma once

#include "render/Shader.h"

#include <glm/glm.hpp>

#include <string>

namespace ParallelRoam::Render
{
/// <summary>
/// 单帧渲染上下文，承载相机矩阵和当前 drawable 尺寸
/// </summary>
struct TriangleRenderContext
{
    glm::mat4 View{1.0F};
    glm::mat4 Projection{1.0F};
    int DrawableWidth{1};
    int DrawableHeight{1};
};

/// <summary>
/// 用于验证 OpenGL 闭环的最小地形占位渲染器
/// </summary>
class TriangleRenderer
{
public:
    TriangleRenderer() = default;
    ~TriangleRenderer();

    TriangleRenderer(const TriangleRenderer&) = delete;
    TriangleRenderer& operator=(const TriangleRenderer&) = delete;

    /// <summary>
    /// 创建 shader、VAO 和 VBO
    /// </summary>
    /// <param name="errorMessage">失败时写入 shader 编译或链接日志。</param>
    bool Initialize(std::string* errorMessage);

    /// <summary>
    /// 释放 OpenGL buffer、VAO 和 shader program
    /// </summary>
    void Shutdown();

    /// <summary>
    /// 绘制阶段 0 的地面三角形
    /// </summary>
    /// <param name="context">当前帧渲染上下文。</param>
    void Render(const TriangleRenderContext& context);

private:
    // 后续阶段会替换为 terrain renderer，这里只保留验证渲染链路的最小状态
    Shader _shader;
    unsigned int _vertexArrayId{0};
    unsigned int _vertexBufferId{0};
};
} // 命名空间 ParallelRoam::Render
