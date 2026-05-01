#version 450

layout(location = 0) in vec2 localOffset;
layout(location = 0) out vec4 FragColor;

layout(set = 0, binding = 1, std140) uniform PlaneCB
{
	ivec4 ActiveRect;
	ivec2 GameResolution;
	float PaletteOffset;
	int PriorityFlag;
	ivec4 PlayfieldSize;
	int VScrollOffsetBias;
	int ScrollOffsetX;
	int ScrollOffsetY;
	int UseHorizontalScrolling;
	int UseVerticalScrolling;
	int NoRepeat;
	int Padding0;
	int Padding1;
	int Padding2;
};

layout(set = 1, binding = 0) uniform usampler2D PatternCacheTexture;
layout(set = 1, binding = 1) uniform sampler2D PaletteTexture;
layout(set = 1, binding = 2) uniform usampler2D IndexTexture;
layout(set = 1, binding = 3) uniform isampler2D HScrollOffsetsTexture;
layout(set = 1, binding = 4) uniform isampler2D VScrollOffsetsTexture;

vec4 getPaletteColor(uint paletteIndex, float paletteOffsetY)
{
	uint paletteX = paletteIndex & 0xffu;
	uint paletteY = paletteIndex >> 8u;
	vec2 samplePosition = vec2((float(paletteX) + 0.5) / 256.0, (float(paletteY) + 0.5) / 4.0 + paletteOffsetY);
	return textureLod(PaletteTexture, samplePosition, 0.0);
}

void main()
{
	int ix = int(localOffset.x);
	int iy = int(localOffset.y);
	int scrollOffsetLookupX = (UseHorizontalScrolling != 0) ? texelFetch(HScrollOffsetsTexture, ivec2(iy & 0xff, 0), 0).r : ScrollOffsetX;
	int scrollOffsetLookupY = ScrollOffsetY;
	if (UseVerticalScrolling != 0)
	{
		int vx = ix - VScrollOffsetBias;
		vx = (vx & 0x1f0) >> 4;
		scrollOffsetLookupY = texelFetch(VScrollOffsetsTexture, ivec2(vx, 0), 0).r;
	}
	ix += scrollOffsetLookupX;
	iy += scrollOffsetLookupY;
	ix = ix & 0x0fff;
	iy = iy & (PlayfieldSize.y - 1);
	if (NoRepeat != 0)
	{
		if (ix >= PlayfieldSize.x)
			discard;
	}
	else
	{
		ix &= (PlayfieldSize.x - 1);
	}

	int patternX = ix / 8;
	int patternY = iy / 8;
	int localX = ix - patternX * 8;
	int localY = iy - patternY * 8;
	uint patternIndex = texelFetch(IndexTexture, ivec2(patternX + patternY * PlayfieldSize.z, 0), 0).r;
	if ((((patternIndex & 0x8000u) != 0u) ? 1 : 0) != PriorityFlag)
		discard;

	uint atex = (patternIndex >> 9u) & 0x30u;
	localX = ((patternIndex & 0x0800u) == 0u) ? localX : (7 - localX);
	localY = ((patternIndex & 0x1000u) == 0u) ? localY : (7 - localY);
	uint paletteIndex = texelFetch(PatternCacheTexture, ivec2(localX + localY * 8, int(patternIndex & 0x07ffu)), 0).r + atex;
	vec4 color = getPaletteColor(paletteIndex, PaletteOffset);
	if (color.a < 0.01)
		discard;
	FragColor = color;
}
