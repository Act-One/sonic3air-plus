#version 450

layout(location = 0) in vec2 uv0;
layout(location = 0) out vec4 FragColor;

layout(set = 1, binding = 0) uniform sampler2D MainTexture;

void main()
{
	FragColor = textureLod(MainTexture, uv0, 0.0);
}
