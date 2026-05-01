#version 450

layout(location = 0) in vec2 position;
layout(location = 0) out vec3 localOffset;

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

void main()
{
	localOffset.x = position.x * float(Size.x);
	localOffset.y = position.y * float(Size.y);
	vec2 v = localOffset.xy + vec2(PivotOffset);
	vec2 transformedVertex;
	transformedVertex.x = v.x * Transformation.x + v.y * Transformation.y;
	transformedVertex.y = v.x * Transformation.z + v.y * Transformation.w;
	transformedVertex.x = float(Position.x) + transformedVertex.x;
	transformedVertex.y = float(Position.y) + transformedVertex.y;
	gl_Position = vec4(
		transformedVertex.x / float(GameResolution.x) * 2.0 - 1.0,
		1.0 - transformedVertex.y / float(GameResolution.y) * 2.0,
		(Position.z != 0) ? 0.75 : 0.5,
		1.0);
	localOffset.z = transformedVertex.y - float(WaterLevel);
}
