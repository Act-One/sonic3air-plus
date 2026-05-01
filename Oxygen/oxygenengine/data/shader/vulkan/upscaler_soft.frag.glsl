#version 450

layout(location = 0) in vec2 uv0;
layout(location = 0) out vec4 FragColor;

layout(set = 0, binding = 1, std140) uniform UpscalerCB
{
	ivec2 GameResolution;
	ivec2 OutputSize;
	float PixelFactor;
	float ScanlinesIntensity;
	vec2 Padding0;
};

layout(set = 1, binding = 0) uniform sampler2D MainTexture;

void main()
{
	vec2 resolution = vec2(GameResolution);
	vec2 uv;

	float x = uv0.x * resolution.x;
	float ix = floor(x + 0.5);
	float fx = x - ix;
	fx = clamp(fx * PixelFactor, -0.5, 0.5);
	uv.x = (ix + fx) / resolution.x;

	float y = uv0.y * resolution.y;
	float iy = floor(y + 0.5);
	float fy = y - iy;
	float colorMultiplier = 1.0;
	if (ScanlinesIntensity > 0.0)
	{
		colorMultiplier = 1.0 - (0.5 - abs(fy)) * ScanlinesIntensity;
	}

	fy = clamp(fy * PixelFactor, -0.5, 0.5);
	uv.y = (iy + fy) / resolution.y;

	vec4 color = textureLod(MainTexture, uv, 0.0);
	if (ScanlinesIntensity > 0.0)
	{
		color.rgb *= colorMultiplier;
	}
	color.a = 1.0;
	FragColor = color;
}
