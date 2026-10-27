// Existing pre-foreground depth copy, plus the exact per-texel stereo shift.
// R retains the original 32-bit scene depth; G retains a 32-bit computed shift.
// The search still uses every original step and all original edge tests.
sampler2D depthTex : register(s1);
float4 sourceMap : register(c0);
float4 tuning : register(c1); // base, slope, maximum shift, near softness
float4 main(float2 uv : TEXCOORD0) : COLOR {
    float d=tex2Dlod(depthTex,float4(uv*sourceMap.xy+sourceMap.zw,0,0)).r;
    float raw=max(tuning.x+tuning.y*saturate(d),0.0);
    float span=tuning.z*tuning.w;
    float knee=tuning.z-span;
    float excess=max(raw-knee,0.0);
    if(span>0.0)raw=min(raw,knee)+span*excess/(span+excess);
    return float4(d,min(raw,tuning.z),d,1);
}
