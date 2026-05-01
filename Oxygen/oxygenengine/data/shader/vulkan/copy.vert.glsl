#version 450

layout(location = 0) in vec2 position;
layout(location = 0) out vec2 uv0;

void main()
{
	uv0 = position;
	gl_Position = vec4(position * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);
}
