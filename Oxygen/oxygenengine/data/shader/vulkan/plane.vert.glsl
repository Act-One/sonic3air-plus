#version 450

layout(location = 0) in vec2 position;
layout(location = 0) out vec2 localOffset;

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

void main()
{
	ivec2 screenPosition;
	screenPosition.x = ActiveRect.x + int(position.x * float(ActiveRect.z) + 0.5);
	screenPosition.y = ActiveRect.y + int(position.y * float(ActiveRect.w) + 0.5);
	localOffset = vec2(screenPosition);
	gl_Position = vec4(
		float(screenPosition.x) / float(GameResolution.x) * 2.0 - 1.0,
		1.0 - float(screenPosition.y) / float(GameResolution.y) * 2.0,
		(PriorityFlag != 0) ? 0.75 : 0.0,
		1.0);
}
