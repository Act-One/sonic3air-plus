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
layout(set = 1, binding = 1) uniform sampler2D LUT;

const float SCALE = 2.0;
const mat3 yuv_matrix = mat3(0.299, -0.169, 0.5, 0.587, -0.331, -0.419, 0.114, 0.5, -0.081);
const vec3 yuv_threshold = vec3(48.0 / 255.0, 7.0 / 255.0, 6.0 / 255.0);
const vec3 yuv_offset = vec3(0.0, 0.5, 0.5);

bool diff3(vec3 yuv1, vec3 yuv2)
{
	bvec3 res = greaterThan(abs((yuv1 + yuv_offset) - (yuv2 + yuv_offset)), yuv_threshold);
	return res.x || res.y || res.z;
}

void main()
{
	vec2 gameResolution = vec2(GameResolution);
	vec2 fp = fract(uv0 * gameResolution);
	vec2 quad = sign(-0.5 + fp);
	vec2 ps = 1.0 / gameResolution;
	float dx = ps.x;
	float dy = ps.y;

	vec3 p1 = textureLod(MainTexture, uv0, 0.0).rgb;
	vec3 p2 = textureLod(MainTexture, uv0 + vec2(dx, dy) * quad, 0.0).rgb;
	vec3 p3 = textureLod(MainTexture, uv0 + vec2(dx, 0.0) * quad, 0.0).rgb;
	vec3 p4 = textureLod(MainTexture, uv0 + vec2(0.0, dy) * quad, 0.0).rgb;

	vec3 w1 = yuv_matrix * textureLod(MainTexture, uv0 + vec2(-dx, -dy), 0.0).rgb;
	vec3 w2 = yuv_matrix * textureLod(MainTexture, uv0 + vec2(0.0, -dy), 0.0).rgb;
	vec3 w3 = yuv_matrix * textureLod(MainTexture, uv0 + vec2(dx, -dy), 0.0).rgb;
	vec3 w4 = yuv_matrix * textureLod(MainTexture, uv0 + vec2(-dx, 0.0), 0.0).rgb;
	vec3 w5 = yuv_matrix * p1;
	vec3 w6 = yuv_matrix * textureLod(MainTexture, uv0 + vec2(dx, 0.0), 0.0).rgb;
	vec3 w7 = yuv_matrix * textureLod(MainTexture, uv0 + vec2(-dx, dy), 0.0).rgb;
	vec3 w8 = yuv_matrix * textureLod(MainTexture, uv0 + vec2(0.0, dy), 0.0).rgb;
	vec3 w9 = yuv_matrix * textureLod(MainTexture, uv0 + vec2(dx, dy), 0.0).rgb;

	vec3 pattern0 = vec3(diff3(w5, w1) ? 1.0 : 0.0, diff3(w5, w2) ? 1.0 : 0.0, diff3(w5, w3) ? 1.0 : 0.0);
	vec3 pattern1 = vec3(diff3(w5, w4) ? 1.0 : 0.0, 0.0, diff3(w5, w6) ? 1.0 : 0.0);
	vec3 pattern2 = vec3(diff3(w5, w7) ? 1.0 : 0.0, diff3(w5, w8) ? 1.0 : 0.0, diff3(w5, w9) ? 1.0 : 0.0);
	vec4 cross = vec4(diff3(w4, w2) ? 1.0 : 0.0, diff3(w2, w6) ? 1.0 : 0.0, diff3(w8, w4) ? 1.0 : 0.0, diff3(w6, w8) ? 1.0 : 0.0);

	vec2 index;
	index.x = dot(pattern0, vec3(1.0, 2.0, 4.0)) + dot(pattern1, vec3(8.0, 0.0, 16.0)) + dot(pattern2, vec3(32.0, 64.0, 128.0));
	index.y = dot(cross, vec4(1.0, 2.0, 4.0, 8.0)) * (SCALE * SCALE) + dot(floor(fp * SCALE), vec2(1.0, SCALE));

	vec2 stepSize = 1.0 / vec2(256.0, 16.0 * (SCALE * SCALE));
	vec2 offset = stepSize * 0.5;
	vec4 weights = textureLod(LUT, index * stepSize + offset, 0.0);
	float sum = dot(weights, vec4(1.0));
	vec3 res = (p1 * weights.x + p2 * weights.y + p3 * weights.z + p4 * weights.w) / sum;
	FragColor = vec4(res, 1.0);
}
