#version 450

layout(location = 0) in vec3 localOffset;
layout(location = 0) out vec4 FragColor;

layout(set = 0, binding = 1, std140) uniform SpriteCB
{
	ivec2 Size;
	int AlphaTest;
	int Padding0;
	ivec3 Position;
	int WaterLevel;
	ivec2 PivotOffset;
	ivec2 GameResolution;
	vec4 Transformation;
	vec4 TintColor;
	vec4 AddedColor;
	int Atex;
	int ShadowHighlightMode;
	ivec2 Padding1;
};

layout(set = 1, binding = 0) uniform sampler2D SpriteTexture;

void main()
{
	ivec2 coord = clamp(ivec2(localOffset.xy), ivec2(0, 0), Size - ivec2(1, 1));
	vec4 color = texelFetch(SpriteTexture, coord, 0);
	color = vec4(AddedColor.rgb, 0.0) + color * TintColor;
	if (AlphaTest != 0 && color.a < 0.01)
		discard;
	FragColor = color;
}
