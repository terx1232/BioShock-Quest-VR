sampler2D image : register(s0);
sampler2D coverage : register(s1);
sampler2D worldDepth : register(s2);
float4 params : register(c0); // near-plane scale, world-depth availability
float4 sourceMap : register(c1); // source rectangle scale, world-eye x offset
float4 main(float2 uv : TEXCOORD0) : COLOR {
    // Coverage comes from actual geometry, not the material's alpha (opaque
    // BioShock materials may leave an arbitrary alpha in the backbuffer).
    float2 source=uv*sourceMap.xy;
    float z=tex2D(coverage,source).r;
    // Use the world's original near/far projection for scene occlusion.
    float originalZ=1.0+(z-1.0)/params.x;
    float scene=tex2D(worldDepth,float2(uv.x*.5+sourceMap.z,uv.y)).r;
    bool visible=z<0.999999&&(params.y<0.5||originalZ<=scene+0.00002);
    return float4(tex2D(image,source).rgb,visible?1.0:0.0);
}
