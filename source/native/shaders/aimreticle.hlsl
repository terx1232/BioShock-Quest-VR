// A small, scissored marker at the sampled world surface. Both eyes use the
// same source depth and the exact disparity/near knee as the world pass.
sampler2D depthTex : register(s0);
sampler2D worldDepth : register(s1);
float4 shiftParams : register(c0); // base, slope, eye direction, max shift
float4 depthMap : register(c1);
float4 target : register(c2); // centre-eye UV, radius in output pixels, near softness
float4 pixels : register(c3); // eye width, height, cached disparity, unused
float4 tint : register(c4);
float4 worldMap : register(c5); // per-eye scale, offset x, occlusion available

float4 main(float2 uv : TEXCOORD0) : COLOR0 {
    float2 value=tex2Dlod(depthTex,float4(target.xy*depthMap.xy,0,0)).rg;
    float raw=max(shiftParams.x+shiftParams.y*saturate(value.r),0);
    float span=shiftParams.w*target.w;
    float knee=shiftParams.w-span;
    float excess=max(raw-knee,0);
    if(span>0)raw=min(raw,knee)+span*excess/(span+excess);
    float shift=pixels.z>.5?value.g:min(raw,shiftParams.w);
    float2 centre=target.xy+float2(shiftParams.z*shift,0);
    float radius=length((uv-centre)*pixels.xy);
    // Do not paint the far-side marker over a nearer reconstructed silhouette.
    if(worldMap.w>.5){
        float visible=tex2Dlod(worldDepth,float4(uv*worldMap.xy+float2(worldMap.z,0),0,0)).r;
        clip(visible-value.r+.0001);
    }
    float dotAlpha=1-smoothstep(.7,1.4,radius);
    float ring=1-smoothstep(.6,1.25,abs(radius-target.z));
    float outline=1-smoothstep(1.3,2.3,abs(radius-target.z));
    float alpha=max(dotAlpha,outline*.9);
    clip(alpha-.01);
    return float4(tint.rgb*max(dotAlpha,ring),alpha);
}
