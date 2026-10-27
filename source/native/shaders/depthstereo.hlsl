// Both eyes from one centre-camera frame, without temporal history. Fixed-point
// iteration oscillates between foreground and background at depth boundaries.
sampler2D colorTex : register(s0);
sampler2D depthTex : register(s1);
float4 shiftParams : register(c0); // base, slope, eye direction, maximum UV shift
float4 depthMap : register(c1);    // scale and offset into the depth texture
float4 colorMap : register(c2);    // scale into the active colour rectangle
float4 texelSize : register(c3);   // xy: 1 / active source dimensions; z: near softness
float4 shiftCache : register(c4); // x: same-frame RG32F copy has exact matching shift parameters

float2 SourceUV(float2 uv) {
    // Clamp to active texel centres, including at reduced dynamic resolution.
    return clamp(uv, texelSize.xy * 0.5, 1.0 - texelSize.xy * 0.5);
}

float ShiftAt(float x, float y) {
    float2 uv = SourceUV(float2(x, y)) * depthMap.xy + depthMap.zw;
    float2 sample = tex2Dlod(depthTex, float4(uv, 0, 0)).rg;
    [branch] if(shiftCache.x>0.5)return sample.g;
    float d = sample.r;
    float raw = max(shiftParams.x + shiftParams.y * saturate(d), 0.0);
    // Keep distant stereo unchanged. A continuous near knee avoids the abrupt
    // disparity plateau on slanted close surfaces and narrows disocclusion
    // holes, whose hidden content is absent from the centre-camera image.
    float span = shiftParams.w * texelSize.z;
    float knee = shiftParams.w - span;
    float excess = max(raw-knee,0.0);
    if(span>0.0)raw=min(raw,knee)+span*excess/(span+excess);
    return min(raw,shiftParams.w);
}

float4 EdgeColor(float2 uv) {
    // Bilinear colour filtering must not blend a weapon texel with a wall
    // across a depth break. Keep the four usual taps, reject other surfaces,
    // and renormalize; smooth regions retain ordinary bilinear filtering.
    float2 pixel = uv / texelSize.xy - 0.5;
    float2 f = frac(pixel);
    float2 p = (floor(pixel) + 0.5) * texelSize.xy;
    float2 a = SourceUV(p);
    float2 b = SourceUV(p + float2(texelSize.x, 0));
    float2 c = SourceUV(p + float2(0, texelSize.y));
    float2 d = SourceUV(p + texelSize.xy);
    float ref = ShiftAt(uv.x, uv.y);
    float4 shifts = float4(ShiftAt(a.x,a.y), ShiftAt(b.x,b.y),
                           ShiftAt(c.x,c.y), ShiftAt(d.x,d.y));
    float4 weights = float4((1-f.x)*(1-f.y), f.x*(1-f.y), (1-f.x)*f.y, f.x*f.y);
    weights *= step(abs(shifts - ref), texelSize.x);
    float total = dot(weights, 1.0);
    if (total < 0.0001) return tex2Dlod(colorTex,float4(SourceUV(uv)*colorMap.xy,0,0));
    return (tex2Dlod(colorTex,float4(a*colorMap.xy,0,0))*weights.x
          + tex2Dlod(colorTex,float4(b*colorMap.xy,0,0))*weights.y
          + tex2Dlod(colorTex,float4(c*colorMap.xy,0,0))*weights.z
          + tex2Dlod(colorTex,float4(d*colorMap.xy,0,0))*weights.w) / total;
}

struct StereoOutput {float4 color:COLOR0;float4 depth:COLOR1;};
StereoOutput main(float2 uv : TEXCOORD0) {
    float dir = shiftParams.z;
    // Search nearest to farthest: the first intersection occludes the rest.
    float lo = shiftParams.w;
    float hi = lo;
    // Include the near endpoint. Skipping it loses narrow weapons whose
    // disparity is clamped to the maximum, even with a small search step.
    int steps = min(128, max(1, (int)ceil(shiftParams.w / texelSize.x)));
    float stepSize = shiftParams.w / steps;
    [loop] for (int i = 1; i <= steps; ++i) {
        if (ShiftAt(uv.x - dir * hi, uv.y) >= hi) break;
        lo = hi;
        hi = max(shiftParams.w - stepSize * i, 0.0);
    }
    [unroll] for (int j = 0; j < 5; ++j) {
        float mid = (lo + hi) * 0.5;
        if (ShiftAt(uv.x - dir * mid, uv.y) >= mid) hi = mid;
        else lo = mid;
    }
    float slo = ShiftAt(uv.x - dir * lo, uv.y);
    float shi = ShiftAt(uv.x - dir * hi, uv.y);
    float errLo = abs(slo - lo), errHi = abs(shi - hi);
    float t = errLo < errHi ? lo : hi;
    float x = uv.x - dir * t;
    // A jump in depth can cross the ray without any visible source pixel mapping
    // here. Extend the background edge into that hole, never the foreground.
    if (min(errLo, errHi) > texelSize.x * 0.75) {
        t = slo < shi ? lo : hi;
        x = uv.x - dir * t;
        x = (floor(x / texelSize.x) + 0.5) * texelSize.x;
    }
    float2 source = SourceUV(float2(x, uv.y));
    float4 c = EdgeColor(source);
    c.a = 1.0;
    StereoOutput o;o.color=c;
    o.depth=tex2Dlod(depthTex,float4(source*depthMap.xy+depthMap.zw,0,0)).rrrr;
    return o;
}
