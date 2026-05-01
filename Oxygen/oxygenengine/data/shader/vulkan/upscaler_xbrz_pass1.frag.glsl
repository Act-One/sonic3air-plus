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
layout(set = 1, binding = 1) uniform sampler2D OrigTexture;

const float LUMINANCE_WEIGHT = 1.0;

float DistYCbCr(vec3 pixA, vec3 pixB)
{
	const vec3 w = vec3(0.2627, 0.6780, 0.0593);
	const float scaleB = 0.5 / (1.0 - w.b);
	const float scaleR = 0.5 / (1.0 - w.r);
	vec3 diff = pixA - pixB;
	float Y = dot(diff.rgb, w);
	float Cb = scaleB * (diff.b - Y);
	float Cr = scaleR * (diff.r - Y);
	return sqrt(((LUMINANCE_WEIGHT * Y) * (LUMINANCE_WEIGHT * Y)) + (Cb * Cb) + (Cr * Cr));
}

float get_left_ratio(vec2 center, vec2 origin, vec2 direction, vec2 scale)
{
	vec2 P0 = center - origin;
	vec2 proj = direction * (dot(P0, direction) / dot(direction, direction));
	vec2 distv = P0 - proj;
	vec2 orth = vec2(-direction.y, direction.x);
	float side = sign(dot(P0, orth));
	float v = side * length(distv * scale);
	return smoothstep(-sqrt(2.0) / 2.0, sqrt(2.0) / 2.0, v);
}

void main()
{
	vec2 originalSize = vec2(GameResolution);
	vec2 outputSize = vec2(OutputSize);
	vec2 invOriginalSize = 1.0 / originalSize;
	vec2 texCoord = uv0 * 1.0001;
	vec2 scale = outputSize * invOriginalSize;
	vec2 pos = fract(texCoord * originalSize) - vec2(0.5, 0.5);
	vec2 coord = texCoord - pos * invOriginalSize;

	vec3 B = textureLod(OrigTexture, coord + invOriginalSize * vec2( 0.0, -1.0), 0.0).rgb;
	vec3 D = textureLod(OrigTexture, coord + invOriginalSize * vec2(-1.0,  0.0), 0.0).rgb;
	vec3 E = textureLod(OrigTexture, coord, 0.0).rgb;
	vec3 F = textureLod(OrigTexture, coord + invOriginalSize * vec2( 1.0,  0.0), 0.0).rgb;
	vec3 H = textureLod(OrigTexture, coord + invOriginalSize * vec2( 0.0,  1.0), 0.0).rgb;

	vec4 info = floor(textureLod(MainTexture, coord, 0.0) * 255.0 + 0.5);
	vec4 blendResult = floor(mod(info, 4.0));
	vec4 doLineBlend = floor(mod(info / 4.0, 4.0));
	vec4 haveShallowLine = floor(mod(info / 16.0, 4.0));
	vec4 haveSteepLine = floor(mod(info / 64.0, 4.0));

	vec3 res = E;

	if (blendResult.z > 0.0)
	{
		vec2 origin = vec2(0.0, 1.0 / sqrt(2.0));
		vec2 direction = vec2(1.0, -1.0);
		if (doLineBlend.z > 0.0)
		{
			origin = (haveShallowLine.z > 0.0) ? vec2(0.0, 0.25) : vec2(0.0, 0.5);
			direction.x += haveShallowLine.z;
			direction.y -= haveSteepLine.z;
		}
		vec3 blendPix = mix(H, F, step(DistYCbCr(E, F), DistYCbCr(E, H)));
		res = mix(res, blendPix, get_left_ratio(pos, origin, direction, scale));
	}

	if (blendResult.w > 0.0)
	{
		vec2 origin = vec2(-1.0 / sqrt(2.0), 0.0);
		vec2 direction = vec2(1.0, 1.0);
		if (doLineBlend.w > 0.0)
		{
			origin = (haveShallowLine.w > 0.0) ? vec2(-0.25, 0.0) : vec2(-0.5, 0.0);
			direction.y += haveShallowLine.w;
			direction.x += haveSteepLine.w;
		}
		vec3 blendPix = mix(H, D, step(DistYCbCr(E, D), DistYCbCr(E, H)));
		res = mix(res, blendPix, get_left_ratio(pos, origin, direction, scale));
	}

	if (blendResult.y > 0.0)
	{
		vec2 origin = vec2(1.0 / sqrt(2.0), 0.0);
		vec2 direction = vec2(-1.0, -1.0);
		if (doLineBlend.y > 0.0)
		{
			origin = (haveShallowLine.y > 0.0) ? vec2(0.25, 0.0) : vec2(0.5, 0.0);
			direction.y -= haveShallowLine.y;
			direction.x -= haveSteepLine.y;
		}
		vec3 blendPix = mix(F, B, step(DistYCbCr(E, B), DistYCbCr(E, F)));
		res = mix(res, blendPix, get_left_ratio(pos, origin, direction, scale));
	}

	if (blendResult.x > 0.0)
	{
		vec2 origin = vec2(0.0, -1.0 / sqrt(2.0));
		vec2 direction = vec2(-1.0, 1.0);
		if (doLineBlend.x > 0.0)
		{
			origin = (haveShallowLine.x > 0.0) ? vec2(0.0, -0.25) : vec2(0.0, -0.5);
			direction.x -= haveShallowLine.x;
			direction.y += haveSteepLine.x;
		}
		vec3 blendPix = mix(D, B, step(DistYCbCr(E, B), DistYCbCr(E, D)));
		res = mix(res, blendPix, get_left_ratio(pos, origin, direction, scale));
	}

	FragColor = vec4(res, 1.0);
}
