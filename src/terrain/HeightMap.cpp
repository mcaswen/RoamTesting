#include "terrain/HeightMap.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wsign-conversion"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cstdint>

namespace ParallelRoam::Terrain
{
bool HeightMap::LoadFromFile(const std::filesystem::path& filePath, std::string* errorMessage)
{
    int width = 0;
    int height = 0;
    int channelCount = 0;

    // 使用 16-bit 接口读取，8-bit 图片会由 stb 扩展成统一范围
    stbi_us* pixels = stbi_load_16(filePath.string().c_str(), &width, &height, &channelCount, 1);
    if (pixels == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Height map load failed: " + filePath.string() + "\n" + stbi_failure_reason();
        }
        return false;
    }

    _heightValues.clear();
    _heightValues.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

    // 高度统一归一化到 0..1，terrain size 和 height scale 由 mesh builder 决定
    for (std::size_t index = 0; index < _heightValues.size(); ++index)
    {
        _heightValues[index] = static_cast<float>(pixels[index]) / 65535.0F;
    }

    stbi_image_free(pixels);

    _sourcePath = filePath;
    _width = width;
    _height = height;
    return true;
}

float HeightMap::SamplePixel(int x, int y) const
{
    if (!IsValid())
    {
        return 0.0F;
    }

    const int clampedX = std::clamp(x, 0, _width - 1);
    const int clampedY = std::clamp(y, 0, _height - 1);
    const auto index = static_cast<std::size_t>(clampedY) * static_cast<std::size_t>(_width) +
                       static_cast<std::size_t>(clampedX);
    return _heightValues[index];
}

float HeightMap::SampleBilinear(float u, float v) const
{
    if (!IsValid())
    {
        return 0.0F;
    }

    const float clampedU = std::clamp(u, 0.0F, 1.0F);
    const float clampedV = std::clamp(v, 0.0F, 1.0F);
    const float x = clampedU * static_cast<float>(_width - 1);
    const float y = clampedV * static_cast<float>(_height - 1);

    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);

    const float h00 = SamplePixel(x0, y0);
    const float h10 = SamplePixel(x1, y0);
    const float h01 = SamplePixel(x0, y1);
    const float h11 = SamplePixel(x1, y1);
    const float h0 = h00 * (1.0F - tx) + h10 * tx;
    const float h1 = h01 * (1.0F - tx) + h11 * tx;
    return h0 * (1.0F - ty) + h1 * ty;
}

int HeightMap::Width() const
{
    return _width;
}

int HeightMap::Height() const
{
    return _height;
}

bool HeightMap::IsValid() const
{
    return _width > 0 && _height > 0 && !_heightValues.empty();
}

const std::filesystem::path& HeightMap::SourcePath() const
{
    return _sourcePath;
}
} // 命名空间 ParallelRoam::Terrain
