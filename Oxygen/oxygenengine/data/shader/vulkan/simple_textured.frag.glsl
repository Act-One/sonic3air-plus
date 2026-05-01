#version 450

layout(location = 0) in vec2 uv0;
layout(location = 0) out vec4 FragColor;

layout(set = 0, binding = 1, std140) uniform SimpleRectCB
{
	vec4 Transform;
	vec4 TintColor;
	vec4 AddedColor;
	ivec2 TextureSize;
	int AlphaTest;
	int ShadowHighlightMode;
};

layout(set = 1, binding = 0) uniform sampler2D MainTexture;

void main()
{
	vec4 color = textureLod(MainTexture, uv0, 0.0);
	color = vec4(AddedColor.rgb, 0.0) + color * TintColor;
	if (AlphaTest != 0 && color.a < 0.01)
		discard;
	FragColor = color;
}
