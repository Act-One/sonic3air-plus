#version 450

layout(location = 0) in vec2 position;
layout(location = 0) out vec3 localOffset;

layout(set = 0, binding = 1, std140) uniform SpriteCB
{
	ivec2 Size;
	int FirstPattern;
	int Padding0;
	ivec3 Position;
	int WaterLevel;
	ivec2 GameResolution;
	ivec2 Padding1;
	vec4 TintColor;
	vec4 AddedColor;
	int ShadowHighlightMode;
	ivec3 Padding2;
};

void main()
{
	localOffset.x = position.x * float(Size.x * 8);
	localOffset.y = position.y * float(Size.y * 8);
	vec2 transformedVertex = position;
	if ((FirstPattern & 0x0800) != 0)
		transformedVertex.x = 1.0 - transformedVertex.x;
	if ((FirstPattern & 0x1000) != 0)
		transformedVertex.y = 1.0 - transformedVertex.y;
	transformedVertex.x = float(Position.x) + transformedVertex.x * float(Size.x * 8);
	transformedVertex.y = float(Position.y) + transformedVertex.y * float(Size.y * 8);
	gl_Position = vec4(
		transformedVertex.x / float(GameResolution.x) * 2.0 - 1.0,
		1.0 - transformedVertex.y / float(GameResolution.y) * 2.0,
		(Position.z != 0) ? 0.75 : 0.5,
		1.0);
	localOffset.z = transformedVertex.y - float(WaterLevel);
}
