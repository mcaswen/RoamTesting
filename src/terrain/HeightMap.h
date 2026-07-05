#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ParallelRoam::Terrain
{
/// <summary>
/// 保存归一化高度值的灰度 Height Map
/// </summary>
class HeightMap
{
public:
    /// <summary>
    /// 从图片文件加载单通道高度数据
    /// </summary>
    /// <param name="filePath">高度图路径。</param>
    /// <param name="errorMessage">失败时写入错误信息。</param>
    bool LoadFromFile(const std::filesystem::path& filePath, std::string* errorMessage);

    /// <summary>
    /// 读取像素高度，坐标越界时会 clamp 到边界
    /// </summary>
    /// <param name="x">像素横坐标。</param>
    /// <param name="y">像素纵坐标。</param>
    [[nodiscard]] float SamplePixel(int x, int y) const;

    /// <summary>
    /// 以归一化 UV 读取双线性高度
    /// </summary>
    /// <param name="u">横向归一化坐标。</param>
    /// <param name="v">纵向归一化坐标。</param>
    [[nodiscard]] float SampleBilinear(float u, float v) const;

    [[nodiscard]] int Width() const;
    [[nodiscard]] int Height() const;
    [[nodiscard]] bool IsValid() const;
    [[nodiscard]] const std::filesystem::path& SourcePath() const;

private:
    std::vector<float> _heightValues;
    std::filesystem::path _sourcePath;
    int _width{0};
    int _height{0};
};
} // 命名空间 ParallelRoam::Terrain
