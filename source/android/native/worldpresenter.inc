// Included in cinema.cpp's GL-thread namespace. Immutable GPU snapshots carry
// their own pixel tag. PBO fences are polled; the render thread never waits.
struct WorldSnapshot {
    GLuint texture=0,fbo=0,pbo=0;GLsync fence=nullptr;
    bool ready=false;unsigned tag=0;uint64_t order=0;
};
struct WorldOutput {
    XrSwapchain chain=XR_NULL_HANDLE;XrSwapchainImageOpenGLESKHR images[8]{};
    WorldSnapshot slots[3];unsigned count=0;int width=0,height=0,shown=-1,lastCpu=-1;
    GLint encoding=GL_LINEAR;uint64_t order=0,shownOrder=0;unsigned copied=0,submitted=0;
    double report=0;PFN_xrDestroySwapchain destroy=nullptr;
} worldOutput;
void resetWorld(bool deleteGl){
    auto& w=worldOutput;
    if(deleteGl)for(auto& s:w.slots){
        if(s.fence)glDeleteSync(s.fence);if(s.texture)glDeleteTextures(1,&s.texture);
        if(s.fbo)glDeleteFramebuffers(1,&s.fbo);if(s.pbo)glDeleteBuffers(1,&s.pbo);
    }
    if(w.chain&&w.destroy)w.destroy(w.chain);
    w=WorldOutput{};
}
struct WorldPackGuard {
    GLint pbo,unpackPbo,pack[4],unpack[6];GLboolean dither;
    const GLenum keys[4]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_PIXELS,GL_PACK_SKIP_ROWS};
    const GLenum unpackKeys[6]={GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_IMAGE_HEIGHT,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_IMAGES};
    WorldPackGuard(){glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pbo);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpackPbo);for(int i=0;i<4;i++)glGetIntegerv(keys[i],&pack[i]);for(int i=0;i<6;i++)glGetIntegerv(unpackKeys[i],&unpack[i]);dither=glIsEnabled(GL_DITHER);}
    ~WorldPackGuard(){glBindBuffer(GL_PIXEL_PACK_BUFFER,pbo);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpackPbo);for(int i=0;i<4;i++)glPixelStorei(keys[i],pack[i]);for(int i=0;i<6;i++)glPixelStorei(unpackKeys[i],unpack[i]);if(dither)glEnable(GL_DITHER);else glDisable(GL_DITHER);}
    void tight(){glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);for(int i=0;i<4;i++)glPixelStorei(keys[i],i==0?4:0);for(int i=0;i<6;i++)glPixelStorei(unpackKeys[i],i==0?4:0);}
};
bool verifyWorldPixel(GLint encoding){
    SrgbWriteGuard colorEncoding;
    auto& s=worldOutput.slots[0];GLuint source=0;
    const unsigned char pixels[16]={171,4,249,255,1,4,255,255,255,4,0,255,0,4,1,255};
    glGenTextures(1,&source);glBindTexture(GL_TEXTURE_2D,source);
    glTexImage2D(GL_TEXTURE_2D,0,encoding==GL_SRGB?GL_SRGB8_ALPHA8:GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,sourceFbo);glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,source,0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,s.fbo);glDisable(GL_SCISSOR_TEST);glDisable(GL_DITHER);
    glBlitFramebuffer(0,0,2,2,0,0,2,2,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,s.fbo);glBindBuffer(GL_PIXEL_PACK_BUFFER,s.pbo);
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    GLenum done=fence?glClientWaitSync(fence,GL_SYNC_FLUSH_COMMANDS_BIT,1000000000):GL_WAIT_FAILED;
    bool good=done==GL_ALREADY_SIGNALED||done==GL_CONDITION_SATISFIED;
    if(good){auto* actual=static_cast<unsigned char*>(glMapBufferRange(GL_PIXEL_PACK_BUFFER,0,4,GL_MAP_READ_BIT));
        good=actual&&!memcmp(actual,pixels,4);if(actual)glUnmapBuffer(GL_PIXEL_PACK_BUFFER);}
    if(fence)glDeleteSync(fence);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,sourceFbo);glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,0,0);glDeleteTextures(1,&source);
    good=good&&glGetError()==GL_NO_ERROR;
    if(good)INFO("world GPU tag self-test passed: immutable blit and PBO preserve all metadata bytes");
    else ERROR("world GPU tag self-test failed; stock path retained");
    return good;
}
bool initWorld(int width,int height,GLint encoding){
    auto& w=worldOutput;
    if(w.chain)return w.width==width&&w.height==height&&w.encoding==encoding;
    if(!copyProgram&&!initMenu(1280,720))return false;
    auto instance=at<XrInstance>(engine,0);
    if(XR_FAILED(getProc(instance,"xrDestroySwapchain",reinterpret_cast<PFN_xrVoidFunction*>(&w.destroy))))return false;
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format=copyColorFormat;info.sampleCount=1;info.width=width;info.height=height;info.faceCount=1;info.arraySize=1;info.mipCount=1;
    if(XR_FAILED(createSwapchain(session,&info,&w.chain)))return false;
    if(XR_FAILED(enumerateImages(w.chain,0,&w.count,nullptr))||!w.count||w.count>8){resetWorld(true);return false;}
    for(auto& image:w.images)image.type=XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
    if(XR_FAILED(enumerateImages(w.chain,w.count,&w.count,reinterpret_cast<XrSwapchainImageBaseHeader*>(w.images)))){resetWorld(true);return false;}
    w.width=width;w.height=height;w.encoding=encoding;
    for(auto& s:w.slots){
        glGenTextures(1,&s.texture);glBindTexture(GL_TEXTURE_2D,s.texture);
        glTexStorage2D(GL_TEXTURE_2D,1,encoding==GL_SRGB?GL_SRGB8_ALPHA8:GL_RGBA8,width,height);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1,&s.fbo);glBindFramebuffer(GL_FRAMEBUFFER,s.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,s.texture,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){resetWorld(true);return false;}
        glGenBuffers(1,&s.pbo);glBindBuffer(GL_PIXEL_PACK_BUFFER,s.pbo);glBufferData(GL_PIXEL_PACK_BUFFER,4,nullptr,GL_STREAM_READ);
    }
    if(!verifyWorldPixel(encoding)){resetWorld(true);return false;}
    INFO("world GPU snapshots ready: %dx%d, three slots, async pixel tags, one OpenXR projection",width,height);
    return glGetError()==GL_NO_ERROR;
}
bool pollWorld(){
    for(auto& s:worldOutput.slots)if(s.fence){
        GLenum status=glClientWaitSync(s.fence,0,0);
        if(status==GL_WAIT_FAILED)return false;
        if(status!=GL_ALREADY_SIGNALED&&status!=GL_CONDITION_SATISFIED)continue;
        glBindBuffer(GL_PIXEL_PACK_BUFFER,s.pbo);
        auto* p=static_cast<unsigned char*>(glMapBufferRange(GL_PIXEL_PACK_BUFFER,0,4,GL_MAP_READ_BIT));
        if(!p)return false;
        s.ready=p[1]==4&&p[3]==255;s.tag=(unsigned(p[0])<<8)|p[2];
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);glDeleteSync(s.fence);s.fence=nullptr;
    }
    return true;
}
bool captureWorld(GLuint texture,int cpuTag){
    SrgbWriteGuard colorEncoding;
    auto& w=worldOutput;if(cpuTag==w.lastCpu)return true;
    int target=-1;
    for(int i=0;i<3;i++)if(i!=w.shown&&!w.slots[i].fence&&!w.slots[i].ready){target=i;break;}
    // A lost UDP packet must not fill every ready slot and stall the queue.
    if(target<0)for(int i=0;i<3;i++)if(i!=w.shown&&!w.slots[i].fence&&
        (target<0||w.slots[i].order<w.slots[target].order))target=i;
    if(target>=0){
        auto& s=w.slots[target];s.ready=false;
        glBindFramebuffer(GL_READ_FRAMEBUFFER,sourceFbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,s.fbo);glDisable(GL_SCISSOR_TEST);glDisable(GL_DITHER);
        glBlitFramebuffer(0,0,w.width,w.height,0,0,w.width,w.height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,s.fbo);glBindBuffer(GL_PIXEL_PACK_BUFFER,s.pbo);
        glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        s.fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);s.order=++w.order;
        if(!s.fence||glGetError()!=GL_NO_ERROR)return false;
        glFlush();w.lastCpu=cpuTag;return true;
    }
    return true; // Backpressure: keep the completed image; never wait for GPU.
}
bool uploadWorld(int slot){
    auto& w=worldOutput;uint32_t index;
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if(XR_FAILED(acquireImage(w.chain,&acquire,&index)))return false;
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;
    if(XR_FAILED(waitImage(w.chain,&wait)))return false;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,menuFbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,w.images[index].image,0);
    bool good=glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    if(good){glDisable(GL_SCISSOR_TEST);glDisable(GL_DITHER);copyImage(w.slots[slot].texture,w.width,w.height,w.encoding!=GL_SRGB,0,0,3);good=glGetError()==GL_NO_ERROR;}
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    good=XR_SUCCEEDED(releaseImage(w.chain,&release))&&good;
    if(good)w.copied++;return good;
}
int drawWorld(GLuint texture,int width,int height,int cpuTag){
    GLGuard guard;WorldPackGuard packing;packing.tight();
    if(!init()||!texture||width<1024||width>4096||width%2||height<512||height>2160)return 0;
    for(int i=0;i<8&&glGetError()!=GL_NO_ERROR;i++){}
    if(!sourceFbo)glGenFramebuffers(1,&sourceFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,sourceFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    if(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return 0;
    GLint encoding=GL_LINEAR;glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING,&encoding);
    if(!initWorld(width,height,encoding)||!pollWorld())return 0;
    auto& w=worldOutput;int chosen=w.shown;worldpose::Pair pose{};unsigned imageFlags=0;
    // Select the newest complete image whose full producer id is available.
    for(int i=0;i<3;i++){
        auto& s=w.slots[i];if(!s.ready||s.order<=w.shownOrder)continue;
        worldpose::Pair candidate{};int status=worldpose::SelectImage(s.tag,candidate);
        if(status>0&&(chosen<0||s.order>w.slots[chosen].order)){chosen=i;pose=candidate;}
        else if(status==-2||status==-3||s.order<=w.shownOrder)s.ready=false;
    }
    if(chosen>=0){
        int status=worldpose::SelectImage(w.slots[chosen].tag,pose,&imageFlags);
        if(status<=0){w.slots[chosen].ready=false;chosen=-1;}
    }
    if(chosen>=0&&chosen!=w.shown){
        if(!uploadWorld(chosen))return 0;
        w.shown=chosen;w.shownOrder=w.slots[chosen].order;
        for(int i=0;i<3;i++)if(i!=w.shown&&!w.slots[i].fence&&w.slots[i].order<=w.shownOrder)w.slots[i].ready=false;
    }
    if(!captureWorld(texture,cpuTag))return 0;
    if(chosen<0){w.shown=-1;return submitFrame(nullptr,0);}
    float fx=at<float>(renderer,0x1c)*3.14159265f/360,fy=at<float>(renderer,0x20)*3.14159265f/360;
    if(!(fx>.2f&&fx<1.5f&&fy>.2f&&fy<1.5f))return 0;
    XrCompositionLayerProjectionView views[2]{};
    for(int eye=0;eye<2;eye++){
        auto& v=views[eye];v.type=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        const auto& p=eye?pose.right:pose.left;memcpy(&v.pose,&p,sizeof(p));
        v.fov={-fx,fx,fy,-fy};v.subImage.swapchain=w.chain;
        v.subImage.imageRect={{eye*(w.width/2),0},{w.width/2,w.height}};
    }
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    layer.space=at<XrSpace>(engine,0x18);layer.viewCount=2;layer.views=views;
    const XrCompositionLayerBaseHeader* layers[16]={reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer)};
    unsigned count=appendHud(layers,1,(imageFlags&1)!=0);
    int result=submitFrame(layers,count);w.submitted++;
    double now=seconds();if(now-w.report>5){
        INFO("world projection: frames=%u GPUcopies=%u tag=%u FoV=%.1fx%.1f layers=%u HUD=%s; no CPU wait",w.submitted,w.copied,w.slots[chosen].tag,fx*360/3.14159265f,fy*360/3.14159265f,count,count>1?"native":hudPrepared()?"prepared":"fallback");
        w.report=now;w.submitted=w.copied=0;
    }
    return result==2&&hudPrepared()?3:result;
}
