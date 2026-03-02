#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 outColor;
layout(location = 1) out vec2 outUV;
layout(location = 2) flat out uint outTextureIndex;

layout(set = 0, binding = 0) uniform UBO {
    mat4 viewProj;
} ubo;

layout(set = 0, binding = 1) uniform ObjectUBO {
    mat4 model;
    uvec4 material;
} objectUbo;

void main() {
    gl_Position = ubo.viewProj * objectUbo.model * vec4(inPos, 1.0);
    outColor = inColor;
    outUV = inUV;
    outTextureIndex = objectUbo.material.x;
}
