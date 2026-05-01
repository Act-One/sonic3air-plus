#version 450

layout(location = 0) in vec2 position;
layout(location = 0) out vec2 uv0;

layout(set = 0, binding = 1, std140) uniform SimpleRectCB
{
	vec4 Transform;
	vec4 TintColor;
	vec4 AddedColor;
	ivec2 TextureSize;
	int AlphaTest;
	int ShadowHighlightMode;
};

void main()
{
	uv0 = position;
	gl_Position = vec4(Transform.x + position.x * Transform.z, Transform.y + position.y * Transform.w, 0.0, 1.0);
}
