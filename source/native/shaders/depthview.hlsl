// Отладка стерео: буфер глубины картинкой. R - сдвиг, G - мелкие изменения глубины, B - глубина.
sampler2D colorTex : register(s0);
sampler2D depthTex : register(s1);
float4 shiftParams : register(c0);
float4 depthMap : register(c1);
float4 main(float2 uv : TEXCOORD0) : COLOR {
    float4 t = tex2Dlod(depthTex, float4(uv.x * depthMap.x, uv.y * depthMap.y, 0.5, 0));
    float d = t.r;
    float s = shiftParams.x + shiftParams.y * d;   // сдвиг в долях ширины
    return float4(saturate(s * 50.0), frac(d * 997.0), d, 1.0);
}
