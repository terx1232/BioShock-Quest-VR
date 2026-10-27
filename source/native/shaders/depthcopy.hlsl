// Копия буфера глубины сцены (до того, как игра очистит его перед руками от первого лица).
sampler2D depthTex : register(s1);
float4 sourceMap : register(c0);
float4 main(float2 uv : TEXCOORD0) : COLOR {
    float d = tex2Dlod(depthTex, float4(uv * sourceMap.xy + sourceMap.zw, 0, 0)).r;
    return float4(d, d, d, 1.0);
}
