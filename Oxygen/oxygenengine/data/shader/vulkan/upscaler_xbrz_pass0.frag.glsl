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

const float BLEND_NONE = 0.0;
const float BLEND_NORMAL = 1.0;
const float BLEND_DOMINANT = 2.0;
const float LUMINANCE_WEIGHT = 1.0;
const float EQUAL_COLOR_TOLERANCE = 30.0 / 255.0;
const float STEEP_DIRECTION_THRESHOLD = 2.2;
const float DOMINANT_DIRECTION_THRESHOLD = 3.6;

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

bool IsPixEqual(vec3 pixA, vec3 pixB)
{
	return (DistYCbCr(pixA, pixB) < EQUAL_COLOR_TOLERANCE);
}

vec3 SampleOffset(vec2 coord, vec2 offset, vec2 invResolution)
{
	return textureLod(MainTexture, coord + invResolution * offset, 0.0).rgb;
}

void main()
{
	vec2 resolution = vec2(GameResolution);
	vec2 invResolution = 1.0 / resolution;
	vec2 texCoord = uv0 * 1.0001;
	vec2 pos = fract(texCoord * resolution) - vec2(0.5, 0.5);
	vec2 coord = texCoord - pos * invResolution;

	vec3 A = SampleOffset(coord, vec2(-1.0, -1.0), invResolution);
	vec3 B = SampleOffset(coord, vec2( 0.0, -1.0), invResolution);
	vec3 C = SampleOffset(coord, vec2( 1.0, -1.0), invResolution);
	vec3 D = SampleOffset(coord, vec2(-1.0,  0.0), invResolution);
	vec3 E = SampleOffset(coord, vec2( 0.0,  0.0), invResolution);
	vec3 F = SampleOffset(coord, vec2( 1.0,  0.0), invResolution);
	vec3 G = SampleOffset(coord, vec2(-1.0,  1.0), invResolution);
	vec3 H = SampleOffset(coord, vec2( 0.0,  1.0), invResolution);
	vec3 I = SampleOffset(coord, vec2( 1.0,  1.0), invResolution);

	vec4 blendResult = vec4(BLEND_NONE);

	if (!((E == F && H == I) || (E == H && F == I)))
	{
		float dist_H_F = DistYCbCr(G, E) + DistYCbCr(E, C) + DistYCbCr(SampleOffset(coord, vec2(0.0, 2.0), invResolution), I) + DistYCbCr(I, SampleOffset(coord, vec2(2.0, 0.0), invResolution)) + (4.0 * DistYCbCr(H, F));
		float dist_E_I = DistYCbCr(D, H) + DistYCbCr(H, SampleOffset(coord, vec2(1.0, 2.0), invResolution)) + DistYCbCr(B, F) + DistYCbCr(F, SampleOffset(coord, vec2(2.0, 1.0), invResolution)) + (4.0 * DistYCbCr(E, I));
		bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_H_F) < dist_E_I;
		blendResult.z = ((dist_H_F < dist_E_I) && E != F && E != H) ? (dominantGradient ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
	}

	if (!((D == E && G == H) || (D == G && E == H)))
	{
		float dist_G_E = DistYCbCr(SampleOffset(coord, vec2(-2.0, 1.0), invResolution), D) + DistYCbCr(D, B) + DistYCbCr(SampleOffset(coord, vec2(-1.0, 2.0), invResolution), H) + DistYCbCr(H, F) + (4.0 * DistYCbCr(G, E));
		float dist_D_H = DistYCbCr(SampleOffset(coord, vec2(-2.0, 0.0), invResolution), G) + DistYCbCr(G, SampleOffset(coord, vec2(0.0, 2.0), invResolution)) + DistYCbCr(A, E) + DistYCbCr(E, I) + (4.0 * DistYCbCr(D, H));
		bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_D_H) < dist_G_E;
		blendResult.w = ((dist_G_E > dist_D_H) && E != D && E != H) ? (dominantGradient ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
	}

	if (!((B == C && E == F) || (B == E && C == F)))
	{
		float dist_E_C = DistYCbCr(D, B) + DistYCbCr(B, SampleOffset(coord, vec2(1.0, -2.0), invResolution)) + DistYCbCr(H, F) + DistYCbCr(F, SampleOffset(coord, vec2(2.0, -1.0), invResolution)) + (4.0 * DistYCbCr(E, C));
		float dist_B_F = DistYCbCr(A, E) + DistYCbCr(E, I) + DistYCbCr(SampleOffset(coord, vec2(0.0, -2.0), invResolution), C) + DistYCbCr(C, SampleOffset(coord, vec2(2.0, 0.0), invResolution)) + (4.0 * DistYCbCr(B, F));
		bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_B_F) < dist_E_C;
		blendResult.y = ((dist_E_C > dist_B_F) && E != B && E != F) ? (dominantGradient ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
	}

	if (!((A == B && D == E) || (A == D && B == E)))
	{
		float dist_D_B = DistYCbCr(SampleOffset(coord, vec2(-2.0, 0.0), invResolution), A) + DistYCbCr(A, SampleOffset(coord, vec2(0.0, -2.0), invResolution)) + DistYCbCr(G, E) + DistYCbCr(E, C) + (4.0 * DistYCbCr(D, B));
		float dist_A_E = DistYCbCr(SampleOffset(coord, vec2(-2.0, -1.0), invResolution), D) + DistYCbCr(D, H) + DistYCbCr(SampleOffset(coord, vec2(-1.0, -2.0), invResolution), B) + DistYCbCr(B, F) + (4.0 * DistYCbCr(A, E));
		bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_D_B) < dist_A_E;
		blendResult.x = ((dist_D_B < dist_A_E) && E != D && E != B) ? (dominantGradient ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
	}

	FragColor = blendResult;

	if (blendResult.z == BLEND_DOMINANT || (blendResult.z == BLEND_NORMAL &&
		!((blendResult.y != BLEND_NONE && !IsPixEqual(E, G)) || (blendResult.w != BLEND_NONE && !IsPixEqual(E, C)) ||
		 (IsPixEqual(G, H) && IsPixEqual(H, I) && IsPixEqual(I, F) && IsPixEqual(F, C) && !IsPixEqual(E, I)))))
	{
		FragColor.z += 4.0;
		float dist_F_G = DistYCbCr(F, G);
		float dist_H_C = DistYCbCr(H, C);
		if ((STEEP_DIRECTION_THRESHOLD * dist_F_G <= dist_H_C) && E != G && D != G)
			FragColor.z += 16.0;
		if ((STEEP_DIRECTION_THRESHOLD * dist_H_C <= dist_F_G) && E != C && B != C)
			FragColor.z += 64.0;
	}

	if (blendResult.w == BLEND_DOMINANT || (blendResult.w == BLEND_NORMAL &&
		!((blendResult.z != BLEND_NONE && !IsPixEqual(E, A)) || (blendResult.x != BLEND_NONE && !IsPixEqual(E, I)) ||
		 (IsPixEqual(A, D) && IsPixEqual(D, G) && IsPixEqual(G, H) && IsPixEqual(H, I) && !IsPixEqual(E, G)))))
	{
		FragColor.w += 4.0;
		float dist_H_A = DistYCbCr(H, A);
		float dist_D_I = DistYCbCr(D, I);
		if ((STEEP_DIRECTION_THRESHOLD * dist_H_A <= dist_D_I) && E != A && B != A)
			FragColor.w += 16.0;
		if ((STEEP_DIRECTION_THRESHOLD * dist_D_I <= dist_H_A) && E != I && F != I)
			FragColor.w += 64.0;
	}

	if (blendResult.y == BLEND_DOMINANT || (blendResult.y == BLEND_NORMAL &&
		!((blendResult.x != BLEND_NONE && !IsPixEqual(E, I)) || (blendResult.z != BLEND_NONE && !IsPixEqual(E, A)) ||
		  (IsPixEqual(I, F) && IsPixEqual(F, C) && IsPixEqual(C, B) && IsPixEqual(B, A) && !IsPixEqual(E, C)))))
	{
		FragColor.y += 4.0;
		float dist_B_I = DistYCbCr(B, I);
		float dist_F_A = DistYCbCr(F, A);
		if ((STEEP_DIRECTION_THRESHOLD * dist_B_I <= dist_F_A) && E != I && H != I)
			FragColor.y += 16.0;
		if ((STEEP_DIRECTION_THRESHOLD * dist_F_A <= dist_B_I) && E != A && D != A)
			FragColor.y += 64.0;
	}

	if (blendResult.x == BLEND_DOMINANT || (blendResult.x == BLEND_NORMAL &&
		!((blendResult.w != BLEND_NONE && !IsPixEqual(E, C)) || (blendResult.y != BLEND_NONE && !IsPixEqual(E, G)) ||
		 (IsPixEqual(C, B) && IsPixEqual(B, A) && IsPixEqual(A, D) && IsPixEqual(D, G) && !IsPixEqual(E, A)))))
	{
		FragColor.x += 4.0;
		float dist_D_C = DistYCbCr(D, C);
		float dist_B_G = DistYCbCr(B, G);
		if ((STEEP_DIRECTION_THRESHOLD * dist_D_C <= dist_B_G) && E != C && F != C)
			FragColor.x += 16.0;
		if ((STEEP_DIRECTION_THRESHOLD * dist_B_G <= dist_D_C) && E != G && H != G)
			FragColor.x += 64.0;
	}

	FragColor /= 255.0;
}
