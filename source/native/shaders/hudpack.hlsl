sampler2D hud : register(s0);
float4 region : register(c0);
float4 main(float2 uv:TEXCOORD0):COLOR {
    float4 c=tex2D(hud,region.xy+uv*region.zw);
    return float4(c.aaa,1);
}
