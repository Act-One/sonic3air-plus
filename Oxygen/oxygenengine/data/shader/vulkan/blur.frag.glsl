#version 450

layout(location = 0) in vec2 uv0;
layout(location = 0) out vec4 FragColor;

layout(set = 0, binding = 1, std140) uniform BlurCB
{
	vec2 TexelOffset;
	vec2 Padding0;
	vec4 Kernel;
};

layout(set = 1, binding = 0) uniform sampler2D MainTexture;

void main()
{
	vec3 color00 = textureLod(MainTexture, uv0 + vec2(-TexelOffset.x, -TexelOffset.y), 0.0).rgb;
	vec3 color01 = textureLod(MainTexture, uv0 + vec2(0.0, -TexelOffset.y), 0.0).rgb;
	vec3 color02 = textureLod(MainTexture, uv0 + vec2(TexelOffset.x, -TexelOffset.y), 0.0).rgb;
	vec3 color10 = textureLod(MainTexture, uv0 + vec2(-TexelOffset.x, 0.0), 0.0).rgb;
	vec3 color11 = textureLod(MainTexture, uv0, 0.0).rgb;
	vec3 color12 = textureLod(MainTexture, uv0 + vec2(TexelOffset.x, 0.0), 0.0).rgb;
	vec3 color20 = textureLod(MainTexture, uv0 + vec2(-TexelOffset.x, TexelOffset.y), 0.0).rgb;
	vec3 color21 = textureLod(MainTexture, uv0 + vec2(0.0, TexelOffset.y), 0.0).rgb;
	vec3 color22 = textureLod(MainTexture, uv0 + vec2(TexelOffset.x, TexelOffset.y), 0.0).rgb;
	vec3 color = color00 * Kernel.w + color01 * Kernel.z + color02 * Kernel.w
			   + color10 * Kernel.y + color11 * Kernel.x + color12 * Kernel.y
			   + color20 * Kernel.w + color21 * Kernel.z + color22 * Kernel.w;
	FragColor = vec4(color, 1.0);
}
