// BioshockVR.fx - depth based stereo 3D for BioShock on WinlatorXR.
//
// Turns every game frame into a half side-by-side stereo pair: the left half of the output is
// the left eye, the right half the right eye. WinlatorXR's SBS mode then shows each half to
// its own eye, so the world gets real depth in the headset. Each eye is reconstructed from the
// colour and depth buffers by shifting pixels by their parallax, refined a few times so edges
// of near objects stay put.

#ifndef RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN
	#define RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN 0
#endif
#ifndef RESHADE_DEPTH_INPUT_IS_REVERSED
	#define RESHADE_DEPTH_INPUT_IS_REVERSED 0
#endif
#ifndef RESHADE_DEPTH_INPUT_IS_LOGARITHMIC
	#define RESHADE_DEPTH_INPUT_IS_LOGARITHMIC 0
#endif
#ifndef RESHADE_DEPTH_LINEARIZATION_FAR_PLANE
	#define RESHADE_DEPTH_LINEARIZATION_FAR_PLANE 1000.0
#endif

uniform float Divergence <
	ui_type = "slider"; ui_min = 0.0; ui_max = 0.08; ui_step = 0.001;
	ui_label = "Stereo strength";
	ui_tooltip = "How far apart the eyes are. Higher is deeper but tires the eyes.";
> = 0.025;

uniform float ScreenDepth <
	ui_type = "slider"; ui_min = 0.0; ui_max = 0.2; ui_step = 0.001;
	ui_label = "Screen plane depth";
	ui_tooltip = "Depth that sits at the screen. Anything nearer pops out (weapons, hands).";
> = 0.015;

uniform bool SwapEyes <
	ui_label = "Swap eyes";
> = false;

uniform bool ShowDepth <
	ui_label = "Show depth (debug)";
	ui_tooltip = "Should show near = black, far = white. If it is empty, depth is not reaching ReShade.";
> = false;

texture BackBufferTex : COLOR;
texture DepthBufferTex : DEPTH;

sampler BackBuffer { Texture = BackBufferTex; AddressU = CLAMP; AddressV = CLAMP; };
sampler DepthBuffer { Texture = DepthBufferTex; AddressU = CLAMP; AddressV = CLAMP; MinFilter = POINT; MagFilter = POINT; };

float LinearDepth(float2 uv)
{
#if RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN
	uv.y = 1.0 - uv.y;
#endif
	float depth = tex2Dlod(DepthBuffer, float4(uv, 0.0, 0.0)).x;
#if RESHADE_DEPTH_INPUT_IS_LOGARITHMIC
	const float C = 0.01;
	depth = (exp(depth * log(C + 1.0)) - 1.0) / C;
#endif
#if RESHADE_DEPTH_INPUT_IS_REVERSED
	depth = 1.0 - depth;
#endif
	const float N = 1.0;
	depth /= RESHADE_DEPTH_LINEARIZATION_FAR_PLANE - depth * (RESHADE_DEPTH_LINEARIZATION_FAR_PLANE - N);
	return saturate(depth);
}

// Horizontal shift in texture units: zero at the screen plane, up to Divergence far away and
// negative (out of the screen) for things nearer than the screen plane.
float Parallax(float2 uv)
{
	float z = LinearDepth(uv);
	return clamp(Divergence * (1.0 - ScreenDepth / max(z, 0.0001)), -Divergence, Divergence);
}

void PostProcessVS(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD)
{
	texcoord.x = (id == 2) ? 2.0 : 0.0;
	texcoord.y = (id == 1) ? 2.0 : 0.0;
	position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 StereoPS(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
	bool rightHalf = texcoord.x >= 0.5;
	float2 uv = float2(frac(texcoord.x * 2.0), texcoord.y);

	if (ShowDepth)
	{
		float z = LinearDepth(uv);
		return float4(z, z, z, 1.0);
	}

	// Left eye samples to the right of the pixel, the right eye to the left
	float eye = (rightHalf != SwapEyes) ? -0.5 : 0.5;
	float shift = Parallax(uv);
	[unroll]
	for (int i = 0; i < 3; i++)
		shift = Parallax(float2(uv.x + eye * shift, uv.y));

	return tex2Dlod(BackBuffer, float4(uv.x + eye * shift, uv.y, 0.0, 0.0));
}

technique BioshockVR <
	ui_label = "BioShock VR stereo";
	ui_tooltip = "Side-by-side stereo for WinlatorXR SBS mode.";
>
{
	pass
	{
		VertexShader = PostProcessVS;
		PixelShader = StereoPS;
	}
}
