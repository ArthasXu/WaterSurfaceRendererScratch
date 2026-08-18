#version 450

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 uv;

layout(set = 0, binding = 0) uniform CameraUBO
{
    mat4 model;
    mat4 view;
    mat4 projection;
    ivec4 debug;
} camera;

layout(location = 0) out vec4 outColor;

vec3 WorldChecker(){ // 世界空间棋盘格
    // 棋盘格以 8 米为间距，随世界坐标固定，便于观察水面网格在世界中的比例和位置
    // 移动物体时棋盘格不随相机移动，能暴露坐标错误
    vec2 cell = floor(worldPosition.xz / 8.0);
    float checker = mod(cell.x + cell.y, 2.0);

    vec3 colorA = vec3(0.05, 0.18, 0.24);
    vec3 colorB = vec3(0.08, 0.32, 0.38);

    return mix(colorA, colorB, checker);
}

vec3 WorldXZColor(){ // 红蓝渐变映射 XZ 坐标
    // 快速判断 UV 方向、网格方向和坐标范围。颜色跳变位置标志世界坐标零点。
    float x = fract(worldPosition.x * 0.02);
    float z = fract(worldPosition.z * 0.02);
    return vec3(x, 0.25, z);
}

vec3 NormalColor(){ // 法线可视化
    return normalize(worldNormal) * 0.5 + 0.5;
}

vec3 UVColor(){ // UV 可视化
    return vec3(uv, 0.0);
}

void main(){
    int mode = camera.debug.x;

    vec3 color = vec3(0.05, 0.22, 0.30);

    if(mode == 1){
        color = WorldChecker();
    }
    else if(mode == 2){
        color = WorldXZColor();
    }
    else if(mode == 3){
        color = NormalColor();
    }
    else if(mode == 4){
        color = UVColor();
    }

    outColor = vec4(color, 1.0);
}


