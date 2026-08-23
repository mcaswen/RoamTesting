#pragma once

#include <filesystem>
#include <span>
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
    bool LoadFromFile(const std::filesystem::path& filePath, std::string* errorMessage);

    /// <summary>
    /// 读取像素高度，坐标越界时会 clamp 到边界
    /// </summary>
    [[nodiscard]] float SamplePixel(int x, int y) const;

    /// <summary>
    /// 以归一化 UV 读取双线性高度
    /// </summary>
    [[nodiscard]] float SampleBilinear(float u, float v) const;

    [[nodiscard]] int Width() const;
    [[nodiscard]] int Height() const;
    [[nodiscard]] bool IsValid() const;
    [[nodiscard]] const std::filesystem::path& SourcePath() const;
    /// @brief 按 y * Width + x 的顺序暴露归一化高度，供 GPU 纹理上传使用
    [[nodiscard]] std::span<const float> Values() const;

private:
    std::vector<float> _heightValues;
    std::filesystem::path _sourcePath;
    int _width{0};
    int _height{0};
};
} // 命名空间 ParallelRoam::Terrain
