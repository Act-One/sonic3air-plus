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

layout(set = 1, binding = 0) uniform usampler2D MainTexture;
layout(set = 1, binding = 1) uniform sampler2D PaletteTexture;

vec4 getPaletteColor(uint paletteIndex)
{
	uint paletteX = paletteIndex & 0xffu;
	uint paletteY = paletteIndex >> 8u;
	vec2 samplePosition = vec2((float(paletteX) + 0.5) / 256.0, (float(paletteY) + 0.5) / 4.0);
	return textureLod(PaletteTexture, samplePosition, 0.0);
}

void main()
{
	ivec2 coord = ivec2(clamp(uv0 * vec2(TextureSize), vec2(0.0), vec2(TextureSize) - vec2(1.0)));
	uint paletteIndex = texelFetch(MainTexture, coord, 0).r;
	vec4 color = getPaletteColor(paletteIndex);
	color = vec4(AddedColor.rgb, 0.0) + color * TintColor;
	if (AlphaTest != 0 && color.a < 0.01)
		discard;
	FragColor = color;
}
