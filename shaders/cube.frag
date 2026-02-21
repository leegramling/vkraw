#version 450

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec2 inUV;
layout(location = 0) out vec4 outFragColor;
layout(set = 0, binding = 1) uniform sampler2D earthTex;

void main() {
    vec3 texColor = texture(earthTex, inUV).rgb;
    outFragColor = vec4(texColor * inColor, 1.0);
}
