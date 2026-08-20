#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string_view>

namespace water
{
// 障碍物数据结构
enum class WaterObstacleShape
{
    Circle,
    Box,
    Capsule,
    ConvexPolygon,
    MeshProjection
};

enum class WaterObstacleMode
{
    SolidWall,
    SubmergedBed,
    Porous
};

inline const char* ToString(WaterObstacleShape shape)
{
    switch(shape){
        case WaterObstacleShape::Circle:
            return "Circle";
        case WaterObstacleShape::Box:
            return "Box";
        case WaterObstacleShape::Capsule:
            return "Capsule";
        case WaterObstacleShape::ConvexPolygon:
            return "ConvexPolygon";
        case WaterObstacleShape::MeshProjection:
            return "MeshProjection";
        default:
            return "Circle";
    }
}

inline const char* ToString(WaterObstacleMode mode)
{
    switch(mode){
        case WaterObstacleMode::SolidWall:
            return "SolidWall";
        case WaterObstacleMode::SubmergedBed:
            return "SubmergedBed";
        case WaterObstacleMode::Porous:
            return "Porous";
        default:
            return "SolidWall";
    }
}

inline WaterObstacleShape WaterObstacleShapeFromString(
    std::string_view value
)
{
    if(value == "Circle"){
        return WaterObstacleShape::Circle;
    }

    if(value == "Box"){
        return WaterObstacleShape::Box;
    }

    if(value == "Capsule"){
        return WaterObstacleShape::Capsule;
    }

    if(value == "ConvexPolygon"){
        return WaterObstacleShape::ConvexPolygon;
    }

    if(value == "MeshProjection"){
        return WaterObstacleShape::MeshProjection;
    }

    return WaterObstacleShape::Circle;
}

inline WaterObstacleMode WaterObstacleModeFromString(
    std::string_view value
)
{
    if(value == "SolidWall"){
        return WaterObstacleMode::SolidWall;
    }

    if(value == "SubmergedBed"){
        return WaterObstacleMode::SubmergedBed;
    }

    if(value == "Porous"){
        return WaterObstacleMode::Porous;
    }

    return WaterObstacleMode::SolidWall;
}

struct WaterObstacleAuthoring
{
    uint32_t id = 0;

    WaterObstacleShape shape =
        WaterObstacleShape::Circle;

    WaterObstacleMode mode =
        WaterObstacleMode::SolidWall;

    glm::vec2 centerXZ{0.0f};
    glm::vec2 size{10.0f};

    float radius = 5.0f;
    float rotationRadians = 0.0f;

    float baseHeight = -10.0f;
    float topHeight = 10.0f;

    float permeability = 0.0f;
    float drag = 1.0f;
    float foamGain = 1.0f;
    float sedimentGain = 0.0f;

    bool enabled = true;
};

struct WaterObstacleBakeParams
{
    float influenceRadius = 220.0f;
    float targetCellSize = 2.0f;
    uint32_t minResolution = 128;
    uint32_t maxResolution = 512;
};

struct ObstacleAABB
{
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};
};
}