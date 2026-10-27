// Saved scene depth plus the depth of weapons drawn after the depth clear.
// Both passes use the same near/far planes and the headset FOV.
sampler2D sceneDepth : register(s1);
sampler2D weaponDepth : register(s2);
float4 weaponMap : register(c0); // saved depth UV -> live INTZ UV
float4 main(float2 uv : TEXCOORD0) : COLOR {
    float scene = tex2Dlod(sceneDepth, float4(uv, 0, 0)).r;
    float weapon = tex2Dlod(weaponDepth, float4(uv * weaponMap.xy, 0, 0)).r;
    float d = min(scene, weapon);
    return float4(d, d, d, 1.0);
}
