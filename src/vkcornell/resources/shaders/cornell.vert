#version 460

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 mvp;
} ubo;

layout(location = 0) out vec3 vColor;

void main() {
    vColor = inColor;
    gl_Position = ubo.mvp * vec4(inPos, 1.0);
}
