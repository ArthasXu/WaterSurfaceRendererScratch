#include "scene/water/terrain/Heightmap.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <algorithm>

namespace water
{
Heightmap LoadHeightmap(const std::string& path)
{
    Heightmap hm;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &hm.width, &hm.height, &channels, 1);
    if(!pixels){ hm.width = 0; hm.height = 0; return hm; }

    hm.heights.resize(static_cast<size_t>(hm.width) * hm.height);
    for(size_t i = 0; i < hm.heights.size(); ++i){
        hm.heights[i] = pixels[i] / 255.0f;
    }
    stbi_image_free(pixels);
    return hm;
}

float Heightmap::Sample(float u, float v) const
{
    if(!valid()) return 0.0f;
    float fx = glm::clamp(u, 0.0f, 1.0f) * static_cast<float>(width - 1);
    float fy = glm::clamp(v, 0.0f, 1.0f) * static_cast<float>(height - 1);
    int x0 = static_cast<int>(fx);
    int y0 = static_cast<int>(fy);
    int x1 = std::min(x0 + 1, width - 1);
    int y1 = std::min(y0 + 1, height - 1);
    float tx = fx - x0, ty = fy - y0;
    float h00 = heights[y0 * width + x0];
    float h10 = heights[y0 * width + x1];
    float h01 = heights[y1 * width + x0];
    float h11 = heights[y1 * width + x1];
    return glm::mix(glm::mix(h00, h10, tx), glm::mix(h01, h11, tx), ty);
}
}