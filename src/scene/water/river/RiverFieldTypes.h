#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace water
{
// ===== 河流中轴线控制点 =====
// 定义河流的走向、宽度、涌潮振幅等参数，通过插值形成完整的河流曲线
struct RiverControlPoint
{
    glm::vec2 position{0.0f};       // 控制点的世界坐标位置
    float halfWidth = 100.0f;       // 该点处的河道半宽（米）
    float boreAmplitude = 1.0f;     // 该点处的涌潮振幅缩放系数
    float curvatureWeight = 0.0f;   // 曲率权重，用于控制弯曲处波前形变
};

// ===== 河流曲线采样点 =====
// 由控制点插值生成，包含沿河流中轴线的空间位置和方向信息
struct RiverSamplePoint
{
    glm::vec2 position{0.0f};       // 采样点的世界坐标位置
    glm::vec2 tangent{1.0f, 0.0f};  // 该点处河流中轴线的切线方向（单位向量）

    float progressMeters = 0.0f;    // 从河流起点算起的沿中轴线累积距离（米）
    float halfWidth = 100.0f;       // 该点处的河道半宽（米）
    float boreAmplitude = 1.0f;     // 该点处的涌潮振幅缩放系数
    float curvatureWeight = 0.0f;   // 曲率权重，用于控制弯曲处波前形变
};

// ===== 河流投影结果 =====
// 将任意世界坐标点投影到河流中轴线上得到的局部坐标与属性
struct RiverProjection
{
    bool valid = false;             // 该点是否成功投影到河流中轴线上（是否在河道范围内）

    glm::vec2 center{0.0f};         // 中轴线上最近点的世界坐标
    glm::vec2 tangent{1.0f, 0.0f};  // 该最近点的切线方向（单位向量）
    glm::vec2 normal{0.0f, 1.0f};   // 该最近点的法线方向（即河道横向方向，单位向量）

    float progressMeters = 0.0f;    // 最近点沿中轴线的累积距离（米）
    float lateralMeters = 0.0f;     // 该点到中轴线的横向距离（米），正值表示在法线方向侧
    float halfWidth = 0.0f;         // 最近点处的河道半宽（米）

    float boreAmplitude = 1.0f;     // 最近点处的涌潮振幅缩放系数
    float curvatureWeight = 0.0f;   // 最近点处的曲率权重
};

// ===== 河流场预烘焙纹理的配置 =====
// 控制 Flow Map 和 Coordinate Map 的分辨率、范围以及河岸过渡宽度
struct RiverFieldConfig
{
    glm::vec2 worldMin{-1024.0f, -1024.0f};  // 纹理覆盖区域的世界空间左下角坐标
    float worldSize = 2048.0f;                // 纹理覆盖区域的边长（米），正方形区域

    uint32_t resolution = 1024;               // 纹理分辨率（resolution × resolution）

    float bankFade = 6.0f;                    // 河岸淡出的起始距离（米），用于 smoothstep 的 edge0
    float bankFadeDistance = 24.0f;           // 河岸淡出的总过渡距离（米），超过此距离完全无河
};

// ===== 预烘焙的河流场纹理数据 =====
// 包含 Flow Map（流向）和 Coordinate Map（中轴线坐标），运行时上传为 GPU 纹理
struct RiverFieldData
{
    RiverFieldConfig config{};                 // 生成该数据时使用的配置

    float riverLength = 0.0f;                  // 河流中轴线总长度（米），用于进度归一化

    // ===== River Flow Map =====
    // 格式：RGBA16F
    // R = flowDirection.x     — 流线方向向量的 X 分量（世界空间，归一化）
    // G = flowDirection.z     — 流线方向向量的 Z 分量（世界空间，归一化）
    // B = boreAmplitudeScale  — 涌潮振幅缩放系数（沿河流变化，河口 1.0，上游逐渐衰减）
    // A = waterMask           — 水域掩码（1.0 = 在河道内，0.0 = 在岸上），着色器可用 smoothstep
    //                           过渡或直接 discard
    std::vector<glm::vec4> flow;              

    // ===== River Coordinate Map =====
    // 格式：RGBA16F 或 RGBA32F（若精度要求高）
    // R = normalizedProgress       — 沿河流中轴线从入海口到当前最近点的归一化距离 [0, 1]。
    //                                0 = 入海口，1 = 河道末端。这是潮头推进的“里程表”。
    // G = normalizedLateral        — 顶点在河道横截面上的归一化位置。
    //                                -1 = 左岸，0 = 中轴线，+1 = 右岸。
    // B = normalizedBankDistance   — 到最近河岸线的归一化距离或河道 SDF 值。
    //                                用于河岸泡沫、泥沙混合等效果。
    // A = frontCurvatureWeight     — 前曲率权重。在宽阔海面/河口区域接近 1（波前接近直线），
    //                                进入狭窄弯曲河道后逐渐降为 0（波前需随河道弯曲）。
    std::vector<glm::vec4> coordinate;        
};
}