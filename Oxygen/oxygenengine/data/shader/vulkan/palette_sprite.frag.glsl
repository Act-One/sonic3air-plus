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

layout(set = 1, binding = 0) uniform usampler2D SpriteTexture;
layout(set = 1, binding = 1) uniform sampler2D PaletteTexture;

vec4 getPaletteColor(uint paletteIndex, float paletteOffsetY)
{
	uint paletteX = paletteIndex & 0xffu;
	uint paletteY = paletteIndex >> 8u;
	vec2 samplePosition = vec2((float(paletteX) + 0.5) / 256.0, (float(paletteY) + 0.5) / 4.0 + paletteOffsetY);
	return textureLod(PaletteTexture, samplePosition, 0.0);
}

void main()
{
	ivec2 coord = clamp(ivec2(localOffset.xy), ivec2(0, 0), Size - ivec2(1, 1));
	uint paletteIndex = uint(Atex) + texelFetch(SpriteTexture, coord, 0).r;
	if (ShadowHighlightMode != 0 && (paletteIndex == 0x3eu || paletteIndex == 0x3fu))
	{
		float intensity = (paletteIndex == 0x3fu) ? 1.0 : 0.0;
		FragColor = vec4(intensity, intensity, intensity, 0.5);
		return;
	}
	vec4 color = getPaletteColor(paletteIndex, clamp(localOffset.z, 0.0, 0.5));
	color = vec4(AddedColor.rgb, 0.0) + color * TintColor;
	if (AlphaTest != 0 && color.a < 0.01)
		discard;
	FragColor = color;
}
