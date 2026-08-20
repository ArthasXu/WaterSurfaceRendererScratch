#include "scene/water/obstacle/ObstacleDomainAsset.h"

#include <fstream>

namespace water
{
namespace
{
constexpr char kMagic[8] = {
    'W', 'I', 'D', 'O', 'M', '0', '1', '\0'
};

template<typename T>
void WriteVector(
    std::ofstream& out,
    const std::vector<T>& values
)
{
    uint64_t count =
        static_cast<uint64_t>(
            values.size()
        );

    out.write(
        reinterpret_cast<const char*>(&count),
        sizeof(count)
    );

    if(count > 0){
        out.write(
            reinterpret_cast<const char*>(values.data()),
            static_cast<std::streamsize>(
                count * sizeof(T)
            )
        );
    }
}

template<typename T>
bool ReadVector(
    std::ifstream& in,
    std::vector<T>& values,
    uint64_t expectedCount
)
{
    uint64_t count = 0;

    in.read(
        reinterpret_cast<char*>(&count),
        sizeof(count)
    );

    if(!in || count != expectedCount){
        return false;
    }

    values.resize(
        static_cast<size_t>(count)
    );

    if(count > 0){
        in.read(
            reinterpret_cast<char*>(values.data()),
            static_cast<std::streamsize>(
                count * sizeof(T)
            )
        );
    }

    return static_cast<bool>(in);
}
}

bool SaveInteractionDomainAsset(
    const std::filesystem::path& path,
    const InteractionDomainAsset& asset
)
{
    std::filesystem::create_directories(
        path.parent_path()
    );

    std::ofstream out(
        path,
        std::ios::binary |
        std::ios::trunc
    );

    if(!out){
        return false;
    }

    out.write(kMagic, sizeof(kMagic));

    out.write(
        reinterpret_cast<const char*>(&asset.worldMin),
        sizeof(asset.worldMin)
    );

    out.write(
        reinterpret_cast<const char*>(&asset.worldSize),
        sizeof(asset.worldSize)
    );

    out.write(
        reinterpret_cast<const char*>(&asset.resolution),
        sizeof(asset.resolution)
    );

    WriteVector(out, asset.groundHeight);
    WriteVector(out, asset.obstacleSDF);
    WriteVector(out, asset.obstacleNormal);
    WriteVector(out, asset.solidFraction);
    WriteVector(out, asset.materialId);
    WriteVector(out, asset.drag);
    WriteVector(out, asset.foamGain);
    WriteVector(out, asset.sedimentGain);

    return static_cast<bool>(out);
}

bool LoadInteractionDomainAsset(
    const std::filesystem::path& path,
    InteractionDomainAsset& asset
)
{
    std::ifstream in(
        path,
        std::ios::binary
    );

    if(!in){
        return false;
    }

    char magic[8]{};

    in.read(
        magic,
        sizeof(magic)
    );

    for(size_t i = 0; i < sizeof(kMagic); ++i){
        if(magic[i] != kMagic[i]){
            return false;
        }
    }

    in.read(
        reinterpret_cast<char*>(&asset.worldMin),
        sizeof(asset.worldMin)
    );

    in.read(
        reinterpret_cast<char*>(&asset.worldSize),
        sizeof(asset.worldSize)
    );

    in.read(
        reinterpret_cast<char*>(&asset.resolution),
        sizeof(asset.resolution)
    );

    if(!in || asset.resolution == 0){
        return false;
    }

    uint64_t pixelCount =
        static_cast<uint64_t>(asset.resolution) *
        static_cast<uint64_t>(asset.resolution);

    return
        ReadVector(in, asset.groundHeight, pixelCount) &&
        ReadVector(in, asset.obstacleSDF, pixelCount) &&
        ReadVector(in, asset.obstacleNormal, pixelCount) &&
        ReadVector(in, asset.solidFraction, pixelCount) &&
        ReadVector(in, asset.materialId, pixelCount) &&
        ReadVector(in, asset.drag, pixelCount) &&
        ReadVector(in, asset.foamGain, pixelCount) &&
        ReadVector(in, asset.sedimentGain, pixelCount);
}
}