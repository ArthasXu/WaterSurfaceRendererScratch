#version 450

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraWorldPosition;
    ivec4 debug;
} camera;

layout(location = 0) out vec3 vDir;

void main(){
    // 覆盖全屏的大三角形：顶点 0,1,2
    vec2 uv  = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 ndc = uv * 2.0 - 1.0;
    gl_Position = vec4(ndc, 1.0, 1.0); // z=1 远平面

    // 逆投影→逆视图，得到该像素的世界空间视线方向
    vec4 viewPos = inverse(camera.projection) * vec4(ndc, 1.0, 1.0);
    viewPos /= viewPos.w;
    vDir = mat3(inverse(camera.view)) * viewPos.xyz;
}