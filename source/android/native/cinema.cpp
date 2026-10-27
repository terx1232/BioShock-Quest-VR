// Quest cinema layers. The game supplies a full-resolution menu and its fixed
// screen layout; OpenXR owns the panel pose and the current-frame white beam.
#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <dlfcn.h>
#include <cmath>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include "worldposeaccess.h"
#include "../../native/src/hudlayout.h"
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#define INFO(...) __android_log_print(ANDROID_LOG_INFO,"BioshockCinema",__VA_ARGS__)
#define ERROR(...) __android_log_print(ANDROID_LOG_ERROR,"BioshockCinema",__VA_ARGS__)
#ifdef BIOSHOCK_RELEASE
#define __android_log_print(...) 0
#endif

namespace {
#include "cinemamath.inc"
double seconds(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
template<class T>T at(void* p,int offset){T value;memcpy(&value,(char*)p+offset,sizeof(value));return value;}
PFN_xrGetInstanceProcAddr getProc;
PFN_xrCreateSwapchain createSwapchain;
PFN_xrEnumerateSwapchainFormats enumerateFormats;
PFN_xrEnumerateSwapchainImages enumerateImages;
PFN_xrAcquireSwapchainImage acquireImage;
PFN_xrWaitSwapchainImage waitImage;
PFN_xrReleaseSwapchainImage releaseImage;
PFN_xrEndFrame endFrame;
void* engine;void* renderer;float* cameraOffset;
void (*releaseWindow)(void*);
bool unavailable=false;
XrSession session=XR_NULL_HANDLE;
EGLContext cinemaContext=EGL_NO_CONTEXT;
XrSwapchain chain=XR_NULL_HANDLE;
XrSwapchainImageOpenGLESKHR images[8];
uint32_t imageCount;
XrSwapchain menuChain=XR_NULL_HANDLE;
XrSwapchainImageOpenGLESKHR menuImages[8];
uint32_t menuImageCount;
int menuWidth,menuHeight,lastMenuSequence=-1;
unsigned menuCopies;
GLuint sourceFbo,menuFbo;
GLuint copyProgram;
GLint copyDecode,copyScale,copyBounds;
GLuint fbo,vao,vbo,program;
GLint matrixUniform;
double lastReport;unsigned frames;
constexpr int eyeSide=768;
void resetWorld(bool deleteGl);
void resetHud(bool deleteGl);

#include "cinemaglguard.inc"
#include "cinemacopy.inc"
bool initMenu(int width,int height){
    if(menuChain)return menuWidth==width&&menuHeight==height;
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format=copyColorFormat;info.sampleCount=1;info.width=width;info.height=height;info.faceCount=1;info.arraySize=1;info.mipCount=1;
    XrResult result=createSwapchain(session,&info,&menuChain);
    if(XR_FAILED(result)){ERROR("menu swapchain: %d",result);return false;}
    if(XR_FAILED(enumerateImages(menuChain,0,&menuImageCount,nullptr))||menuImageCount>8)return false;
    for(auto& im:menuImages)im.type=XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
    if(XR_FAILED(enumerateImages(menuChain,menuImageCount,&menuImageCount,(XrSwapchainImageBaseHeader*)menuImages)))return false;
    glGenFramebuffers(1,&sourceFbo);glGenFramebuffers(1,&menuFbo);menuWidth=width;menuHeight=height;
    if(!initCopy()){unavailable=true;return false;}
    INFO("dedicated menu quad %dx%d, direct GPU copy, images %u",width,height,menuImageCount);
    return true;
}
bool copyMenu(GLuint texture,int sourceWidth,int sourceHeight,int sequence,bool fullSource){
    if(sequence==lastMenuSequence)return true;
    uint32_t image;XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if(XR_FAILED(acquireImage(menuChain,&acquire,&image)))return false;
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;
    if(XR_FAILED(waitImage(menuChain,&wait)))return false;
    glBindFramebuffer(GL_READ_FRAMEBUFFER,sourceFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    bool good=glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    GLint encoding=GL_LINEAR;
    if(good)glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING,&encoding);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,menuFbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,menuImages[image].image,0);
    good &= glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    glDisable(GL_SCISSOR_TEST);
    if(good){
        while(glGetError()!=GL_NO_ERROR){}
        copyImage(texture,menuWidth,menuHeight,encoding!=GL_SRGB,sourceWidth,sourceHeight,1,fullSource);
        GLenum error=glGetError();
        if(error!=GL_NO_ERROR){ERROR("menu GPU blit: GL 0x%x",error);good=false;}
    }
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,0,0);
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    good &= XR_SUCCEEDED(releaseImage(menuChain,&release));
    if(!good){ERROR("menu GPU copy failed; retaining fallback");return false;}
    lastMenuSequence=sequence;++menuCopies;return true;
}
bool init(){
    if(!engine){
        void* lib=dlopen("libxr.so",RTLD_NOW);if(!lib){ERROR("libxr not loaded");return false;}
        engine=dlsym(lib,"xr_module_engine");renderer=dlsym(lib,"xr_module_renderer");cameraOffset=(float*)dlsym(lib,"xr_camera_offset");
        releaseWindow=(void(*)(void*))dlsym(lib,"XrRendererEndFrame");getProc=(PFN_xrGetInstanceProcAddr)dlsym(lib,"xrGetInstanceProcAddr");
        if(!engine||!renderer||!cameraOffset||!releaseWindow||!getProc){ERROR("WinlatorXR cinema exports unavailable");unavailable=true;return false;}
    }
    XrSession current=at<XrSession>(engine,8);
    if(!current)return false;
    EGLContext context=eglGetCurrentContext();
    if(session&&(session!=current||cinemaContext!=context)){
        resetWorld(cinemaContext==context);
        resetHud(cinemaContext==context);
        // Winlator recreates its session/context after sleep or a surface change.
        // Old swapchains belong to that destroyed session; recreate our layers.
        if(cinemaContext==context){
            glDeleteProgram(program);glDeleteProgram(copyProgram);glDeleteFramebuffers(1,&fbo);glDeleteFramebuffers(1,&sourceFbo);glDeleteFramebuffers(1,&menuFbo);glDeleteVertexArrays(1,&vao);glDeleteBuffers(1,&vbo);
        }
        session=XR_NULL_HANDLE;chain=menuChain=XR_NULL_HANDLE;fbo=sourceFbo=menuFbo=vao=vbo=program=copyProgram=0;
        menuWidth=menuHeight=0;lastMenuSequence=-1;frames=menuCopies=0;unavailable=false;
        INFO("XR session/context changed; recreating native menu and ray");
    }
    if(unavailable)return false;
    if(chain)return true;
    XrInstance instance=at<XrInstance>(engine,0);
#define PROC(name,var) if(XR_FAILED(getProc(instance,name,(PFN_xrVoidFunction*)&var))){ERROR("missing %s",name);unavailable=true;return false;}
    PROC("xrCreateSwapchain",createSwapchain);PROC("xrEnumerateSwapchainFormats",enumerateFormats);PROC("xrEnumerateSwapchainImages",enumerateImages);PROC("xrAcquireSwapchainImage",acquireImage);PROC("xrWaitSwapchainImage",waitImage);PROC("xrReleaseSwapchainImage",releaseImage);PROC("xrEndFrame",endFrame);
#undef PROC
    uint32_t count=0;if(XR_FAILED(enumerateFormats(current,0,&count,nullptr))||count>128)return false;
    int64_t formats[128];if(XR_FAILED(enumerateFormats(current,count,&count,formats)))return false;
    int64_t format=0;for(uint32_t i=0;i<count;++i)if(formats[i]==GL_RGBA8)format=GL_RGBA8;
    if(!format)for(uint32_t i=0;i<count;++i)if(formats[i]==GL_SRGB8_ALPHA8)format=GL_SRGB8_ALPHA8;
    if(!format){ERROR("no RGBA OpenXR swapchain format");unavailable=true;return false;}
    // Linear RGBA8 rounds dark sRGB values to only a handful of levels.
    // A compositor sRGB image preserves the original display-encoded precision.
    copyColorFormat=0;
    for(uint32_t i=0;i<count;i++)if(formats[i]==GL_SRGB8_ALPHA8)copyColorFormat=GL_SRGB8_ALPHA8;
    if(!copyColorFormat)for(uint32_t i=0;i<count;i++)if(formats[i]==GL_RGBA16F)copyColorFormat=GL_RGBA16F;
    if(!copyColorFormat)copyColorFormat=format;
    INFO("shadow presentation: color format=0x%llx; spatial edge AA for menu/world",static_cast<long long>(copyColorFormat));
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;info.format=format;info.sampleCount=1;info.width=eyeSide*2;info.height=eyeSide;info.faceCount=1;info.arraySize=1;info.mipCount=1;
    XrResult result=createSwapchain(current,&info,&chain);
    if(XR_FAILED(result)){ERROR("ray swapchain: %d",result);unavailable=true;return false;}
    if(XR_FAILED(enumerateImages(chain,0,&imageCount,nullptr))||imageCount>8){unavailable=true;return false;}
    for(auto& im:images)im.type=XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
    if(XR_FAILED(enumerateImages(chain,imageCount,&imageCount,(XrSwapchainImageBaseHeader*)images))){unavailable=true;return false;}
    GLuint vs=shader(GL_VERTEX_SHADER,"#version 300 es\nlayout(location=0) in vec3 position;uniform mat4 mvp;void main(){gl_Position=mvp*vec4(position,1.0);}");
    GLuint fs=shader(GL_FRAGMENT_SHADER,"#version 300 es\nprecision mediump float;out vec4 color;void main(){color=vec4(1.0);}");
    if(!vs||!fs){unavailable=true;return false;}
    program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);glDeleteShader(vs);glDeleteShader(fs);
    GLint good;glGetProgramiv(program,GL_LINK_STATUS,&good);if(!good){unavailable=true;return false;}
    matrixUniform=glGetUniformLocation(program,"mvp");glGenFramebuffers(1,&fbo);glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);session=current;cinemaContext=context;
    INFO("native quad ready: menu full resolution, ray %dx%d, images %u",eyeSide*2,eyeSide,imageCount);
    return true;
}
void drawQuad(V a,V b,V sideA,V sideB){V p[4]={sub(a,sideA),add(a,sideA),sub(b,sideB),add(b,sideB)};glBufferData(GL_ARRAY_BUFFER,sizeof(p),p,GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLE_STRIP,0,4);}
bool renderRay(const XrView* views,const float* axes,const float* layout,int hand,uint32_t image){
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,images[image].image,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return false;
    glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);glUseProgram(program);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(V),nullptr);
    V offset{cameraOffset[0],cameraOffset[1],cameraOffset[2]},center=add({layout[0],layout[1],layout[2]},offset),normal{layout[9],layout[10],layout[11]};
    int q=hand?15:3,p=hand?21:9;V a=add({axes[p],axes[p+1],axes[p+2]},offset);V direction=rotate({axes[q],axes[q+1],axes[q+2],axes[q+3]},{0,0,-1});
    float denom=dot(direction,normal),distance=denom<-.0001f?dot(sub(center,a),normal)/denom:-1;
    V b=add(a,mul(direction,distance>0?distance:3));V right{layout[3],layout[4],layout[5]},up{layout[6],layout[7],layout[8]};V rel=sub(b,center);
    bool hit=distance>0&&fabsf(dot(rel,right))<=layout[12]&&fabsf(dot(rel,up))<=layout[13];if(!hit)b=add(a,mul(direction,3));
    for(int eye=0;eye<2;++eye){
        glViewport(eye*eyeSide,0,eyeSide,eyeSide);float mvp[16];viewProjection(views[eye],mvp);glUniformMatrix4fv(matrixUniform,1,GL_FALSE,mvp);
        V eyePos=vec(views[eye].pose.position),forward=rotate(views[eye].pose.orientation,{0,0,-1});V side=norm(cross(sub(b,a),sub(eyePos,mul(add(a,b),.5f))));
        float angular=3.0f/eyeSide;drawQuad(a,b,mul(side,fmaxf(.001f,dot(sub(a,eyePos),forward)*angular)),mul(side,fmaxf(.001f,dot(sub(b,eyePos),forward)*angular)));
        if(hit){V point=add(b,mul(normal,.004f));drawQuad(sub(point,mul(up,.009f)),add(point,mul(up,.009f)),mul(right,.009f),mul(right,.009f));}
    }
    return true;
}
int submitFrame(const XrCompositionLayerBaseHeader* const* layers,uint32_t count){
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime=at<XrTime>(engine,0x38);
    end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end.layerCount=count;end.layers=layers;
    releaseWindow(renderer);
    XrResult result=endFrame(session,&end);
    for(int offset:{0xa0,0xd0}){
        uint32_t count=at<uint32_t>(renderer,offset+4);
        if(count){uint32_t next=(at<uint32_t>(renderer,offset)+1)%count;memcpy((char*)renderer+offset,&next,4);}
    }
    if(XR_FAILED(result))ERROR("native composition xrEndFrame layers=%u result=%d",count,result);
    // Even an OpenXR error has consumed this begun frame. Never submit twice.
    return 2;
}
#include "hudpresenter.inc"
#include "worldpresenter.inc"
}

extern "C" JNIEXPORT jint JNICALL Java_com_bioshockport_NativeCinema_worldFrame(JNIEnv*,jclass,jint texture,jint width,jint height,jint tag){
    return drawWorld(static_cast<GLuint>(texture),width,height,tag);
}
extern "C" JNIEXPORT void JNICALL Java_com_bioshockport_NativeCinema_prepareHud(JNIEnv* env,jclass,jint texture,jint width,jint height,jint tag,jfloatArray axes){
    hudInput.texture=texture;hudInput.width=width;hudInput.height=height;hudInput.tag=tag;
    hudInput.axesValid=axes&&env->GetArrayLength(axes)>=48;
    if(hudInput.axesValid)env->GetFloatArrayRegion(axes,0,48,hudInput.axes);
}

extern "C" JNIEXPORT jint JNICALL Java_com_bioshockport_NativeCinema_holdFrame(JNIEnv*,jclass){
    GLGuard guard;
    if(!init())return 0;
    // OpenXR requires a cleared display when no composition layers are given.
    return submitFrame(nullptr,0);
}
extern "C" JNIEXPORT void JNICALL Java_com_bioshockport_NativeCinema_restoreWorldViewport(JNIEnv*,jclass){
    // Stock bindFBO only binds the framebuffer. A preceding cinema sink has
    // a 1x1 viewport, so restore the actual stock target dimensions too.
    if(!renderer)return;
    int width=at<int>(renderer,0x5c),height=at<int>(renderer,0x60);
    if(width>=1024&&width<=8192&&height>=512&&height<=8192)glViewport(0,0,width,height);
}
extern "C" JNIEXPORT jint JNICALL Java_com_bioshockport_NativeCinema_frame(JNIEnv* env,jclass,jfloatArray axesArray,jfloatArray layoutArray,jint hand,jboolean submit,jint texture,jint sourceWidth,jint sourceHeight,jint requestedWidth,jint requestedHeight,jint sequence,jboolean fullSource){
    if(env->GetArrayLength(axesArray)<48||env->GetArrayLength(layoutArray)!=14)return 0;
    GLGuard guard;
    if(!init()||texture<=0||sourceWidth<1024||sourceWidth>8192||sourceHeight<512||sourceHeight>8192)return 0;
    int cropWidth=requestedWidth>0?requestedWidth:sourceWidth,cropHeight=requestedHeight>0?requestedHeight:sourceHeight;
    if(cropWidth<320||cropWidth>8192||cropHeight<240||cropHeight>8192||
       (!fullSource&&(cropWidth>sourceWidth||cropHeight>sourceHeight))||!initMenu(cropWidth,cropHeight))return 0;
    XrView* views=at<XrView*>(renderer,0x630);
    int width=at<int>(renderer,0x5c),height=at<int>(renderer,0x60);
    XrSwapchain screen=at<XrSwapchain>(renderer,0x98);
    if(!views||views[0].type!=XR_TYPE_VIEW||views[1].type!=XR_TYPE_VIEW||width<1024||height<512||width>8192||height>8192||!screen)return 0;
    if(!submit)return 1;
    if(!copyMenu((GLuint)texture,sourceWidth,sourceHeight,sequence,fullSource))return 0;
    float axes[53]={},layout[14];env->GetFloatArrayRegion(axesArray,0,48,axes);env->GetFloatArrayRegion(layoutArray,0,14,layout);
    if(!(layout[12]>.05f&&layout[12]<20&&layout[13]>.05f&&layout[13]<20))return 0;
    double started=seconds();uint32_t image;XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};if(XR_FAILED(acquireImage(chain,&acquire,&image)))return 0;
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=XR_INFINITE_DURATION;if(XR_FAILED(waitImage(chain,&wait)))return 0;
    bool good=renderRay(views,axes,layout,hand,image);XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};releaseImage(chain,&release);if(!good)return 0;
    V center{layout[0]+cameraOffset[0],layout[1]+cameraOffset[1],layout[2]+cameraOffset[2]};
    XrSpace space=at<XrSpace>(engine,0x18);
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};quad.space=space;quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;quad.subImage.swapchain=menuChain;quad.subImage.imageRect={{0,0},{menuWidth,menuHeight}};quad.pose.orientation=basis({layout[3],layout[4],layout[5]},{layout[6],layout[7],layout[8]},{layout[9],layout[10],layout[11]});quad.pose.position=xr(center);quad.size={layout[12]*2,layout[13]*2};
    XrCompositionLayerProjectionView projectionViews[2]{};
    for(int eye=0;eye<2;++eye){auto& view=projectionViews[eye];view.type=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;view.pose=views[eye].pose;view.fov=views[eye].fov;view.subImage.swapchain=chain;view.subImage.imageRect={{eye*eyeSide,0},{eyeSide,eyeSide}};}
    XrCompositionLayerProjection ray{XR_TYPE_COMPOSITION_LAYER_PROJECTION};ray.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;ray.space=space;ray.viewCount=2;ray.views=projectionViews;
    const XrCompositionLayerBaseHeader* layers[]={(XrCompositionLayerBaseHeader*)&quad,(XrCompositionLayerBaseHeader*)&ray};
    int result=submitFrame(layers,2);
    ++frames;double now=seconds();if(now-lastReport>5){INFO("quad frames=%u copies=%u, ray current pose, compose %.2f ms, menu %dx%d",frames,menuCopies,(now-started)*1000,menuWidth,menuHeight);frames=menuCopies=0;lastReport=now;}
    // The frame has ended even on an OpenXR error. Never submit it twice.
    return result;
}
