// A separate, full-resolution translucent HUD. Its RGB and alpha arrive in
// two halves of a GPU window; no CPU image readback or world reprojection.
struct HudOutput {
    XrSwapchain chain=XR_NULL_HANDLE;XrSwapchainImageOpenGLESKHR images[8]{};uint32_t count=0;
    int width=0,height=0,lastTag=-1;GLuint program=0;GLint srgb=0,size=0;bool prepared=false;
    PFN_xrDestroySwapchain destroy=nullptr;XrCompositionLayerQuad quads[12]{};bool ready=false;
} hudOutput;
struct HudSource {GLuint texture=0;int width=0,height=0,tag=-1;float axes[48]{};bool axesValid=false;} hudInput;
void resetHud(bool deleteGl){
    if(deleteGl&&hudOutput.program)glDeleteProgram(hudOutput.program);
    if(hudOutput.chain&&hudOutput.destroy)hudOutput.destroy(hudOutput.chain);
    hudOutput=HudOutput{};
}
bool initHud(int width,int height){
    auto& h=hudOutput;if(h.chain&&(h.width!=width||h.height!=height))resetHud(true);if(h.chain)return true;
    if(XR_FAILED(getProc(at<XrInstance>(engine,0),"xrDestroySwapchain",reinterpret_cast<PFN_xrVoidFunction*>(&h.destroy))))return false;
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format=GL_RGBA8;info.sampleCount=1;info.width=width;info.height=height;info.faceCount=1;info.arraySize=1;info.mipCount=1;
    if(XR_FAILED(createSwapchain(session,&info,&h.chain)))return false;
    if(XR_FAILED(enumerateImages(h.chain,0,&h.count,nullptr))||!h.count||h.count>8){resetHud(true);return false;}
    for(auto& im:h.images)im.type=XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
    if(XR_FAILED(enumerateImages(h.chain,h.count,&h.count,reinterpret_cast<XrSwapchainImageBaseHeader*>(h.images)))){resetHud(true);return false;}
    GLuint vs=shader(GL_VERTEX_SHADER,"#version 300 es\nconst vec2 p[4]=vec2[4](vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(1,1));out vec2 uv;void main(){vec2 v=p[gl_VertexID];uv=vec2(v.x*.5+.5,.5-v.y*.5);gl_Position=vec4(v,0,1);}");
    GLuint fs=shader(GL_FRAGMENT_SHADER,"#version 300 es\nprecision highp float;uniform sampler2D source;uniform int sourceSRGB;uniform vec2 dimensions;in vec2 uv;out vec4 color;vec3 enc(vec3 c){return mix(c*12.92,1.055*pow(c,vec3(1./2.4))-.055,step(vec3(.0031308),c));}vec3 dec(vec3 c){return mix(c/12.92,pow((c+.055)/1.055,vec3(2.4)),step(vec3(.04045),c));}void main(){vec2 q=clamp(uv*dimensions,vec2(.5),dimensions-.5);vec2 scale=vec2(dimensions.x,dimensions.y*2.+1.);vec3 rgb=texture(source,q/scale).rgb;vec3 a=texture(source,(q+vec2(0,dimensions.y))/scale).rgb;if(sourceSRGB!=0){rgb=enc(rgb);a=enc(a);}float coverage=clamp(a.r,0.,1.);color=vec4(dec(clamp(rgb/max(coverage,.00392),0.,1.))*coverage,coverage);}");
    if(!vs||!fs){resetHud(true);return false;}
    h.program=glCreateProgram();glAttachShader(h.program,vs);glAttachShader(h.program,fs);glLinkProgram(h.program);glDeleteShader(vs);glDeleteShader(fs);
    GLint good=0;glGetProgramiv(h.program,GL_LINK_STATUS,&good);if(!good){resetHud(true);return false;}
    glUseProgram(h.program);glUniform1i(glGetUniformLocation(h.program,"source"),0);
    h.srgb=glGetUniformLocation(h.program,"sourceSRGB");h.size=glGetUniformLocation(h.program,"dimensions");h.width=width;h.height=height;
    INFO("native HUD quad ready: %dx%d, original glyph pixels, separate coverage",width,height);return true;
}
bool copyHud(){
    auto& h=hudOutput;auto& s=hudInput;
    if(!s.texture||!s.axesValid||s.width<1024||s.width>4096||s.height<1025||s.height>4321||!(s.height&1))return false;
    if(!initHud(s.width,(s.height-1)/2))return false;
    if(h.ready&&h.lastTag==s.tag)return true;
    glBindFramebuffer(GL_READ_FRAMEBUFFER,sourceFbo);glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,s.texture,0);
    if(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return false;
    GLint encoding=GL_LINEAR;glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING,&encoding);
    uint32_t index=0;XrSwapchainImageAcquireInfo a{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};if(XR_FAILED(acquireImage(h.chain,&a,&index)))return false;
    XrSwapchainImageWaitInfo w{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};w.timeout=XR_INFINITE_DURATION;if(XR_FAILED(waitImage(h.chain,&w)))return false;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,menuFbo);glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,h.images[index].image,0);
    bool ok=glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    if(ok){
        glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glViewport(0,0,h.width,h.height);glUseProgram(h.program);glBindVertexArray(vao);glActiveTexture(GL_TEXTURE0);glBindSampler(0,0);glBindTexture(GL_TEXTURE_2D,s.texture);
        glUniform1i(h.srgb,encoding==GL_SRGB);glUniform2f(h.size,h.width,h.height);glDrawArrays(GL_TRIANGLE_STRIP,0,4);ok=glGetError()==GL_NO_ERROR;
    }
    XrSwapchainImageReleaseInfo r{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};ok=XR_SUCCEEDED(releaseImage(h.chain,&r))&&ok;
    if(ok){h.lastTag=s.tag;h.ready=true;}return ok;
}
bool hudPrepared(){return hudOutput.prepared;}
unsigned appendHud(const XrCompositionLayerBaseHeader** layers,unsigned count,bool worldUiHidden){
    hudOutput.prepared=copyHud();if(!hudOutput.prepared||!worldUiHidden)return count;
    auto& h=hudOutput;XrView* views=at<XrView*>(renderer,0x630);if(!views)return count;
    XrQuaternionf orientation=views[0].pose.orientation;
    V head=mul(add(vec(views[0].pose.position),vec(views[1].pose.position)),.5f);
    V right=rotate(orientation,{1,0,0}),up=rotate(orientation,{0,1,0}),forward=rotate(orientation,{0,0,-1});
    float width=3.2f*tanf(at<float>(renderer,0x1c)*3.14159265f/360)*.92f,height=width*h.height/h.width;
    V front=add(head,mul(forward,1.6f));unsigned n=0;
    auto quad=[&](hudlayout::Rect rect,V center,V r,V u,float w,float ht){
        if(n>=12)return;auto& q=h.quads[n++];q={XR_TYPE_COMPOSITION_LAYER_QUAD};q.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        q.space=at<XrSpace>(engine,0x18);q.eyeVisibility=XR_EYE_VISIBILITY_BOTH;q.subImage.swapchain=h.chain;
        int x0=lroundf(rect.x0*h.width),y0=lroundf(rect.y0*h.height),x1=lroundf(rect.x1*h.width),y1=lroundf(rect.y1*h.height);
        // OpenGL ES swapchain origins are bottom-left (OpenXR graphics binding).
        q.subImage.imageRect={{x0,h.height-y1},{x1-x0,y1-y0}};q.pose.orientation=basis(r,u,norm(cross(r,u)));q.pose.position=xr(center);q.size={w,ht};
        layers[count++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&q);
    };
    auto layout=hudlayout::Partition({0,0,.20f,.20f},{.80f,.76f,1,1});
    for(unsigned i=0;i<layout.count;i++){
        auto r=layout.front[i];V c=add(front,add(mul(right,(r.x0+r.x1-1)*width*.5f),mul(up,(1-r.y0-r.y1)*height*.5f)));
        quad(r,c,right,up,(r.x1-r.x0)*width,(r.y1-r.y0)*height);
    }
    for(int hand=0;hand<2;hand++){
        auto r=hand?layout.ammo:layout.health;int qi=hand?44:40,pi=hand?21:9;
        auto* a=hudInput.axes;XrQuaternionf grip{a[qi],a[qi+1],a[qi+2],a[qi+3]};
        V offset{cameraOffset[0],cameraOffset[1],cameraOffset[2]};
        V c=add(add({a[pi],a[pi+1],a[pi+2]},offset),add(mul(rotate(grip,{0,1,0}),.075f),mul(rotate(grip,{0,0,1}),.11f)));
        if(hand)c=add(c,mul(rotate(grip,{1,0,0}),.06f));
        V normal=norm(sub(head,c)),rgt=norm(cross({0,1,0},normal)),upp=norm(cross(normal,rgt));
        float w=hand?.21f:.24f;quad(r,c,rgt,upp,w,w*((r.y1-r.y0)*h.height)/((r.x1-r.x0)*h.width));
    }
    return count;
}
