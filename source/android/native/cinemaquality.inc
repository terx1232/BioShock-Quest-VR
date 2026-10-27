// Spatial edge AA only on new images. No history/reprojection, no eye mixing.
const char* kCopyFragment=R"GLSL(#version 300 es
precision highp float;
uniform sampler2D source;
uniform int decodeSRGB;
uniform int quality;
uniform vec2 sourceScale;
uniform vec4 sourceBounds;
in vec2 uv;out vec4 color;
vec4 bounds;
vec3 sampleImage(vec2 p){return texture(source,clamp(p,bounds.xy,bounds.zw)).rgb;}
float luma(vec3 c){return dot(c,vec3(.299,.587,.114));}
float peak(vec3 c){return max(c.r,max(c.g,c.b));}
void main(){
    vec2 p=uv*sourceScale,texel=1./vec2(textureSize(source,0));bounds=sourceBounds;
    if((quality&2)!=0){
        float split=sourceScale.x*.5;
        if(p.x<split)bounds.z=min(bounds.z,split-texel.x*.5);
        else bounds.x=max(bounds.x,split+texel.x*.5);
    }
    vec3 c=sampleImage(p);
    // Restrict edge smoothing to dark tones. Bright UI glyphs/cursor/ray keep
    // their original strokes; only the shadow staircase gets spatial filtering.
    float shadowLimit=decodeSRGB!=0?.5:.214041;
    if((quality&1)!=0&&peak(c)<shadowLimit){
        vec3 nw=sampleImage(p+vec2(-1,-1)*texel),ne=sampleImage(p+vec2(1,-1)*texel);
        vec3 sw=sampleImage(p+vec2(-1,1)*texel),se=sampleImage(p+vec2(1,1)*texel);
        float a=luma(nw),b=luma(ne),d=luma(sw),e=luma(se),m=luma(c);
        float lo=min(m,min(min(a,b),min(d,e))),hi=max(m,max(max(a,b),max(d,e)));
        float brightest=max(peak(c),max(max(peak(nw),peak(ne)),max(peak(sw),peak(se))));
        if(brightest<shadowLimit&&hi-lo>max(.03125,hi*.125)){
            vec2 direction=vec2(-((a+b)-(d+e)),(a+d)-(b+e));
            float reduce=max((a+b+d+e)*.03125,.0078125);
            direction=clamp(direction/(min(abs(direction.x),abs(direction.y))+reduce),vec2(-4),vec2(4))*texel;
            vec3 aa=.5*(sampleImage(p-direction/6.)+sampleImage(p+direction/6.));
            vec3 bb=.5*aa+.25*(sampleImage(p-direction*.5)+sampleImage(p+direction*.5));
            float value=luma(bb);c=(value<lo||value>hi)?aa:bb;
        }
    }
    if(decodeSRGB!=0)c=mix(c/12.92,pow((c+.055)/1.055,vec3(2.4)),step(vec3(.04045),c));
    color=vec4(c,1);
}
)GLSL";
