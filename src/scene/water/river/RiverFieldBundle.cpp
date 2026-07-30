#include "scene/water/river/RiverFieldBundle.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace water
{
namespace
{
// magic + version; bump the version when the binary layout changes
constexpr char kMagic[8] = {'R', 'V', 'F', 'L', 'D', '0', '1', '\0'};

void WriteArray(
    std::ofstream& out,
    const std::vector<glm::vec4>& array
)
{
    uint64_t count = static_cast<uint64_t>(array.size());
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));
    if(count > 0){
        out.write(
            reinterpret_cast<const char*>(array.data()),
            static_cast<std::streamsize>(count * sizeof(glm::vec4))
        );
    }
}

bool ReadArray(
    std::ifstream& in,
    std::vector<glm::vec4>& array,
    uint64_t expectedCount
)
{
    uint64_t count = 0;
    in.read(reinterpret_cast<char*>(&count), sizeof(count));
    if(!in || count != expectedCount){
        return false;
    }
    array.resize(static_cast<size_t>(count));
    if(count > 0){
        in.read(
            reinterpret_cast<char*>(array.data()),
            static_cast<std::streamsize>(count * sizeof(glm::vec4))
        );
    }
    return static_cast<bool>(in);
}
}

bool SaveRiverFieldBundle(
    const std::string& path,
    const RiverFieldBundle& bundle
)
{
    std::filesystem::path fsPath(path);
    if(fsPath.has_parent_path()){
        std::error_code ec;
        std::filesystem::create_directories(fsPath.parent_path(), ec);
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if(!out){
        return false;
    }

    out.write(kMagic, sizeof(kMagic));

    // field config
    out.write(reinterpret_cast<const char*>(&bundle.config.worldMin), sizeof(glm::vec2));
    out.write(reinterpret_cast<const char*>(&bundle.config.worldSize), sizeof(float));
    out.write(reinterpret_cast<const char*>(&bundle.config.resolution), sizeof(uint32_t));
    out.write(reinterpret_cast<const char*>(&bundle.config.bankFade), sizeof(float));
    out.write(reinterpret_cast<const char*>(&bundle.config.bankFadeDistance), sizeof(float));
    out.write(reinterpret_cast<const char*>(&bundle.riverLength), sizeof(float));

    WriteArray(out, bundle.flow);
    WriteArray(out, bundle.coordinate);
    WriteArray(out, bundle.progress);
    WriteArray(out, bundle.shore);

    return static_cast<bool>(out);
}

bool LoadRiverFieldBundle(
    const std::string& path,
    RiverFieldBundle& outBundle
)
{
    std::ifstream in(path, std::ios::binary);
    if(!in){
        return false;
    }

    char magic[8] = {};
    in.read(magic, sizeof(magic));
    if(!in || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0){
        return false;
    }

    RiverFieldConfig config{};
    in.read(reinterpret_cast<char*>(&config.worldMin), sizeof(glm::vec2));
    in.read(reinterpret_cast<char*>(&config.worldSize), sizeof(float));
    in.read(reinterpret_cast<char*>(&config.resolution), sizeof(uint32_t));
    in.read(reinterpret_cast<char*>(&config.bankFade), sizeof(float));
    in.read(reinterpret_cast<char*>(&config.bankFadeDistance), sizeof(float));

    float riverLength = 0.0f;
    in.read(reinterpret_cast<char*>(&riverLength), sizeof(float));
    if(!in || config.resolution == 0){
        return false;
    }

    uint64_t pixelCount =
        static_cast<uint64_t>(config.resolution) *
        static_cast<uint64_t>(config.resolution);

    RiverFieldBundle bundle{};
    bundle.config = config;
    bundle.riverLength = riverLength;

    if(!ReadArray(in, bundle.flow, pixelCount) ||
       !ReadArray(in, bundle.coordinate, pixelCount) ||
       !ReadArray(in, bundle.progress, pixelCount) ||
       !ReadArray(in, bundle.shore, pixelCount)){
        return false;
    }

    outBundle = std::move(bundle);
    return true;
}
}
