struct V { float x,y,z; };
V add(V a,V b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
V sub(V a,V b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
V mul(V a,float s){return {a.x*s,a.y*s,a.z*s};}
float dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
V norm(V a){float d=sqrtf(dot(a,a));return d>.00001f?mul(a,1/d):V{1,0,0};}
V rotate(XrQuaternionf q,V v){V u{q.x,q.y,q.z};return add(v,add(mul(cross(u,v),2*q.w),mul(cross(u,cross(u,v)),2)));}
V vec(XrVector3f p){return {p.x,p.y,p.z};}
XrVector3f xr(V p){return {p.x,p.y,p.z};}
XrQuaternionf basis(V r,V u,V n){
    XrQuaternionf q{}; float t=r.x+u.y+n.z;
    if(t>0){float s=sqrtf(t+1)*2;q={ (u.z-n.y)/s,(n.x-r.z)/s,(r.y-u.x)/s,.25f*s};}
    else if(r.x>u.y&&r.x>n.z){float s=sqrtf(1+r.x-u.y-n.z)*2;q={.25f*s,(u.x+r.y)/s,(n.x+r.z)/s,(u.z-n.y)/s};}
    else if(u.y>n.z){float s=sqrtf(1+u.y-r.x-n.z)*2;q={(u.x+r.y)/s,.25f*s,(n.y+u.z)/s,(n.x-r.z)/s};}
    else {float s=sqrtf(1+n.z-r.x-u.y)*2;q={(n.x+r.z)/s,(n.y+u.z)/s,.25f*s,(r.y-u.x)/s};}
    return q;
}
void multiply(const float* a,const float* b,float* r){for(int c=0;c<4;++c)for(int row=0;row<4;++row){r[c*4+row]=0;for(int k=0;k<4;++k)r[c*4+row]+=a[k*4+row]*b[c*4+k];}}
void viewProjection(const XrView& eye,float* result){
    V x=rotate(eye.pose.orientation,{1,0,0}),y=rotate(eye.pose.orientation,{0,1,0}),z=rotate(eye.pose.orientation,{0,0,1}),p=vec(eye.pose.position);
    float v[16]={x.x,y.x,z.x,0,x.y,y.y,z.y,0,x.z,y.z,z.z,0,-dot(x,p),-dot(y,p),-dot(z,p),1};
    float l=tanf(eye.fov.angleLeft),r=tanf(eye.fov.angleRight),b=tanf(eye.fov.angleDown),t=tanf(eye.fov.angleUp);
    float pr[16]={2/(r-l),0,0,0,0,2/(t-b),0,0,(r+l)/(r-l),(t+b)/(t-b),-1.001f,-1,0,0,-.10005f,0};
    multiply(pr,v,result);
}
