#include "scene/water/river/RiverSpline.h"

#include <algorithm>
#include <limits>

namespace water
{
namespace
{
glm::vec2 CatmullRomVec2(
    const glm::vec2& p0,
    const glm::vec2& p1,
    const glm::vec2& p2,
    const glm::vec2& p3,
    float t
)
{
    float t2 = t * t;
    float t3 = t2 * t;

    return
        0.5f *
        (
            2.0f * p1 +
            (-p0 + p2) * t +
            (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
            (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
        );
}

float CatmullRomFloat(
    float p0,
    float p1,
    float p2,
    float p3,
    float t
)
{
    float t2 = t * t;
    float t3 = t2 * t;

    return
        0.5f *
        (
            2.0f * p1 +
            (-p0 + p2) * t +
            (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
            (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
        );
}
}

// 构建河流样条线：使用 Catmull‑Rom 样条在控制点间插值，生成密集的采样点数组。
// 相比之前的线性插值，Catmull‑Rom 提供更平滑的曲线和连续变化的切线。
void RiverSpline::Build(
    const std::vector<RiverControlPoint>& controlPoints,
    uint32_t samplesPerSegment
)
{
    m_Samples.clear();
    m_Length = 0.0f;

    // 至少需要 2 个控制点
    if(controlPoints.size() < 2){
        return;
    }

    // 每段至少 8 个采样点（保证平滑度）
    samplesPerSegment = std::max(samplesPerSegment, 8u);

    // 遍历每一对相邻控制点，作为 Catmull‑Rom 的中间两个点 (p1,p2)
    for(size_t i = 0; i + 1 < controlPoints.size(); ++i){
        // 确定 Catmull‑Rom 需要的四个控制点 p0,p1,p2,p3
        // 对于首段和末段，p0 重复 p1，p3 重复 p2，实现边界处理
        const RiverControlPoint& p0 = controlPoints[i == 0 ? i : i - 1];
        const RiverControlPoint& p1 = controlPoints[i];
        const RiverControlPoint& p2 = controlPoints[i + 1];
        const RiverControlPoint& p3 = controlPoints[std::min(i + 2, controlPoints.size() - 1)];

        // 在当前段内生成 samplesPerSegment 个插值点
        for(uint32_t s = 0; s < samplesPerSegment; ++s){
            float t = static_cast<float>(s) / static_cast<float>(samplesPerSegment);

            // 用微小偏移计算数值导数，得到精确的切线方向
            float tPrev = glm::clamp(t - 0.02f, 0.0f, 1.0f);
            float tNext = glm::clamp(t + 0.02f, 0.0f, 1.0f);

            // Catmull‑Rom 插值得到位置和前后点
            glm::vec2 position = CatmullRomVec2(p0.position, p1.position, p2.position, p3.position, t);
            glm::vec2 previous = CatmullRomVec2(p0.position, p1.position, p2.position, p3.position, tPrev);
            glm::vec2 next     = CatmullRomVec2(p0.position, p1.position, p2.position, p3.position, tNext);

            // 切线 = 前后位置差
            glm::vec2 tangent = next - previous;

            RiverSamplePoint sample{};
            sample.position = position;

            // 归一化切线，若退化为零则沿用上一个采样点的切线
            if(glm::length(tangent) > 0.001f){
                sample.tangent = glm::normalize(tangent);
            }
            else if(!m_Samples.empty()){
                sample.tangent = m_Samples.back().tangent;
            }

            // 插值河道半宽，下限 1 米避免完全消失
            sample.halfWidth = std::max(
                CatmullRomFloat(p0.halfWidth, p1.halfWidth, p2.halfWidth, p3.halfWidth, t),
                1.0f
            );

            // 插值涌潮振幅，钳位在 [0, 2] 防止异常放大或负值
            // sample.boreAmplitude = glm::clamp(
            //     CatmullRomFloat(p0.boreAmplitude, p1.boreAmplitude, p2.boreAmplitude, p3.boreAmplitude, t),
            //     0.0f, 2.0f
            // );
            sample.boreAmplitude = CatmullRomFloat(p0.boreAmplitude, p1.boreAmplitude, p2.boreAmplitude, p3.boreAmplitude, t),

            // 插值曲率权重，钳位在 [0, 1]
            sample.curvatureWeight = glm::clamp(
                CatmullRomFloat(p0.curvatureWeight, p1.curvatureWeight, p2.curvatureWeight, p3.curvatureWeight, t),
                0.0f, 1.0f
            );

            // 累加沿河距离（progressMeters）
            if(!m_Samples.empty()){
                m_Length += glm::distance(sample.position, m_Samples.back().position);
            }
            sample.progressMeters = m_Length;

            m_Samples.push_back(sample);
        }
    }

    // 添加最后一个控制点作为终点，确保终点被精确覆盖
    RiverSamplePoint last{};
    last.position       = controlPoints.back().position;
    last.halfWidth      = controlPoints.back().halfWidth;
    last.boreAmplitude  = glm::clamp(controlPoints.back().boreAmplitude,  0.0f, 2.0f);
    last.curvatureWeight = glm::clamp(controlPoints.back().curvatureWeight, 0.0f, 1.0f);

    if(!m_Samples.empty()){
        m_Length += glm::distance(last.position, m_Samples.back().position);
        glm::vec2 tangent = last.position - m_Samples.back().position;
        last.tangent = (glm::length(tangent) > 0.001f) ? glm::normalize(tangent) : m_Samples.back().tangent;
    }

    last.progressMeters = m_Length;
    m_Samples.push_back(last);
}


// 对两个控制点做线性插值，生成中间采样点。
// 位置、半宽、振幅、曲率权重使用线性混合；切线由两点方向归一化得到。
RiverSamplePoint RiverSpline::InterpolateControlPoints(
    const RiverControlPoint& a,
    const RiverControlPoint& b,
    float t
) const
{
    RiverSamplePoint sample{};

    // 位置线性插值
    sample.position = glm::mix(a.position, b.position, t);

    // 切线方向：由两点方向确定（分段直线，切线在段内恒定）
    glm::vec2 tangent = b.position - a.position;
    if(glm::length(tangent) > 0.001f){
        sample.tangent = glm::normalize(tangent);
    }

    // 属性线性插值
    sample.halfWidth       = glm::mix(a.halfWidth, b.halfWidth, t);
    sample.boreAmplitude   = glm::mix(a.boreAmplitude, b.boreAmplitude, t);
    sample.curvatureWeight = glm::mix(a.curvatureWeight, b.curvatureWeight, t);

    return sample;
}

// 将世界空间中的任意点投影到河流中轴线上。
// 遍历所有线段，找到距离最小的线段，计算最近点及其局部坐标。
RiverProjection RiverSpline::Project(
    const glm::vec2& worldXZ
) const
{
    RiverProjection best{};

    if(m_Samples.size() < 2){
        return best;
    }

    float bestDistanceSquared = std::numeric_limits<float>::max();

    // 遍历所有相邻采样点构成的线段
    for(size_t i = 0; i + 1 < m_Samples.size(); ++i){
        const RiverSamplePoint& a = m_Samples[i];
        const RiverSamplePoint& b = m_Samples[i + 1];

        glm::vec2 segment = b.position - a.position;
        float segmentLengthSquared = glm::dot(segment, segment);

        // 跳过过短的退化线段
        if(segmentLengthSquared < 0.0001f){
            continue;
        }

        // 将 worldXZ 投影到当前线段上，t 为投影参数 [0, 1]
        float t =
            glm::clamp(
                glm::dot(worldXZ - a.position, segment) / segmentLengthSquared,
                0.0f, 1.0f
            );

        // 线段上的最近点
        glm::vec2 center = a.position + segment * t;

        float distanceSquared = glm::dot(worldXZ - center, worldXZ - center);

        // 不是更近的点则跳过
        if(distanceSquared >= bestDistanceSquared){
            continue;
        }

        // 计算局部切线、法线和横向距离
        glm::vec2 tangent = glm::normalize(segment);
        glm::vec2 normal  = glm::vec2(-tangent.y, tangent.x);
        float lateralMeters = glm::dot(worldXZ - center, normal);

        bestDistanceSquared = distanceSquared;

        // 填充最佳投影结果
        best.valid           = true;
        best.center          = center;
        best.tangent         = tangent;
        best.normal          = normal;
        best.progressMeters  = glm::mix(a.progressMeters, b.progressMeters, t);
        best.lateralMeters   = lateralMeters;
        best.halfWidth       = glm::mix(a.halfWidth, b.halfWidth, t);
        best.boreAmplitude   = glm::mix(a.boreAmplitude, b.boreAmplitude, t);
        best.curvatureWeight = glm::mix(a.curvatureWeight, b.curvatureWeight, t);
    }

    return best;
}

// 返回河流中轴线总长度
float RiverSpline::GetLength() const
{
    return m_Length;
}

// 返回所有插值采样点的只读引用
const std::vector<RiverSamplePoint>&
RiverSpline::GetSamples() const
{
    return m_Samples;
}
}