#version 450

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec2 inUV;
layout(location = 2) flat in uint inTextureIndex;
layout(location = 0) out vec4 outFragColor;
layout(set = 0, binding = 2) uniform sampler2D textures[32];

void main() {
    uint index = inTextureIndex % 32u;
    vec3 texColor = texture(textures[index], inUV).rgb;
    outFragColor = vec4(texColor * inColor, 1.0);
}
