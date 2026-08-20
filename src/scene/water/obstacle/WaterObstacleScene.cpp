#include "scene/water/obstacle/WaterObstacleScene.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace water
{
namespace
{
nlohmann::json ToJson(
    const WaterObstacleAuthoring& obstacle
)
{
    nlohmann::json json;

    json["id"] = obstacle.id;
    json["shape"] = ToString(obstacle.shape);
    json["mode"] = ToString(obstacle.mode);

    json["centerXZ"] = {
        obstacle.centerXZ.x,
        obstacle.centerXZ.y
    };

    json["size"] = {
        obstacle.size.x,
        obstacle.size.y
    };

    json["radius"] = obstacle.radius;
    json["rotationRadians"] = obstacle.rotationRadians;

    json["baseHeight"] = obstacle.baseHeight;
    json["topHeight"] = obstacle.topHeight;

    json["permeability"] = obstacle.permeability;
    json["drag"] = obstacle.drag;
    json["foamGain"] = obstacle.foamGain;
    json["sedimentGain"] = obstacle.sedimentGain;

    json["enabled"] = obstacle.enabled;

    return json;
}

WaterObstacleAuthoring FromJson(
    const nlohmann::json& json
)
{
    WaterObstacleAuthoring obstacle{};

    obstacle.id =
        json.value(
            "id",
            0u
        );

    obstacle.shape =
        WaterObstacleShapeFromString(
            json.value(
                "shape",
                "Circle"
            )
        );

    obstacle.mode =
        WaterObstacleModeFromString(
            json.value(
                "mode",
                "SolidWall"
            )
        );

    if(json.contains("centerXZ") && json["centerXZ"].is_array()){
        obstacle.centerXZ.x =
            json["centerXZ"].value(
                0,
                0.0f
            );

        obstacle.centerXZ.y =
            json["centerXZ"].value(
                1,
                0.0f
            );
    }

    if(json.contains("size") && json["size"].is_array()){
        obstacle.size.x =
            json["size"].value(
                0,
                10.0f
            );

        obstacle.size.y =
            json["size"].value(
                1,
                10.0f
            );
    }

    obstacle.radius =
        json.value(
            "radius",
            5.0f
        );

    obstacle.rotationRadians =
        json.value(
            "rotationRadians",
            0.0f
        );

    obstacle.baseHeight =
        json.value(
            "baseHeight",
            -10.0f
        );

    obstacle.topHeight =
        json.value(
            "topHeight",
            10.0f
        );

    obstacle.permeability =
        json.value(
            "permeability",
            0.0f
        );

    obstacle.drag =
        json.value(
            "drag",
            1.0f
        );

    obstacle.foamGain =
        json.value(
            "foamGain",
            1.0f
        );

    obstacle.sedimentGain =
        json.value(
            "sedimentGain",
            0.0f
        );

    obstacle.enabled =
        json.value(
            "enabled",
            true
        );

    return obstacle;
}
}

WaterObstacleAuthoring& WaterObstacleScene::AddCircle()
{
    WaterObstacleAuthoring obstacle{};
    obstacle.id = AllocateId();
    obstacle.shape = WaterObstacleShape::Circle;
    obstacle.mode = WaterObstacleMode::SolidWall;
    obstacle.radius = 25.0f;
    obstacle.size = glm::vec2(50.0f);
    m_Obstacles.push_back(obstacle);
    return m_Obstacles.back();
}

WaterObstacleAuthoring& WaterObstacleScene::AddBox()
{
    WaterObstacleAuthoring obstacle{};
    obstacle.id = AllocateId();
    obstacle.shape = WaterObstacleShape::Box;
    obstacle.mode = WaterObstacleMode::SolidWall;
    obstacle.size = glm::vec2(60.0f, 40.0f);
    obstacle.radius = 20.0f;
    m_Obstacles.push_back(obstacle);
    return m_Obstacles.back();
}

WaterObstacleAuthoring& WaterObstacleScene::AddCapsule()
{
    WaterObstacleAuthoring obstacle{};
    obstacle.id = AllocateId();
    obstacle.shape = WaterObstacleShape::Capsule;
    obstacle.mode = WaterObstacleMode::SolidWall;
    obstacle.size = glm::vec2(90.0f, 30.0f);
    obstacle.radius = 15.0f;
    m_Obstacles.push_back(obstacle);
    return m_Obstacles.back();
}

void WaterObstacleScene::Duplicate(
    size_t index
)
{
    if(index >= m_Obstacles.size()){
        return;
    }

    WaterObstacleAuthoring copy =
        m_Obstacles[index];

    copy.id = AllocateId();
    copy.centerXZ += glm::vec2(20.0f, 20.0f);

    m_Obstacles.push_back(copy);
}

void WaterObstacleScene::Delete(
    size_t index
)
{
    if(index >= m_Obstacles.size()){
        return;
    }

    m_Obstacles.erase(
        m_Obstacles.begin() +
        static_cast<std::ptrdiff_t>(index)
    );
}

void WaterObstacleScene::Clear()
{
    m_Obstacles.clear();
    m_NextId = 1;
}

std::vector<WaterObstacleAuthoring>& WaterObstacleScene::Obstacles()
{
    return m_Obstacles;
}

const std::vector<WaterObstacleAuthoring>& WaterObstacleScene::Obstacles() const
{
    return m_Obstacles;
}

bool WaterObstacleScene::SaveJson(
    const std::filesystem::path& path
) const
{
    std::filesystem::create_directories(
        path.parent_path()
    );

    nlohmann::json root;
    root["version"] = 1;
    root["obstacles"] = nlohmann::json::array();

    for(const WaterObstacleAuthoring& obstacle : m_Obstacles){
        root["obstacles"].push_back(
            ToJson(obstacle)
        );
    }

    std::ofstream out(path);

    if(!out){
        return false;
    }

    out << root.dump(4);

    return static_cast<bool>(out);
}

bool WaterObstacleScene::LoadJson(
    const std::filesystem::path& path
)
{
    std::ifstream in(path);

    if(!in){
        return false;
    }

    nlohmann::json root;
    in >> root;

    m_Obstacles.clear();
    m_NextId = 1;

    if(!root.contains("obstacles")){
        return false;
    }

    for(const nlohmann::json& item : root["obstacles"]){
        WaterObstacleAuthoring obstacle =
            FromJson(item);

        m_NextId =
            std::max(
                m_NextId,
                obstacle.id + 1
            );

        m_Obstacles.push_back(obstacle);
    }

    return true;
}

uint32_t WaterObstacleScene::AllocateId()
{
    return m_NextId++;
}
}