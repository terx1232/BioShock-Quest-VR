// Production GLES snapshot/PBO/presentation code with a mocked OpenXR runtime.
// Runs in a hidden EGL pbuffer; never opens BioShock or sends user input.
#include <windows.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <map>
#include <chrono>
#include <initializer_list>
#include "../../android/native/exactpose.h"
#include "../../android/native/worldposeaccess.h"
#define INFO(...) do{printf(__VA_ARGS__);puts("");}while(0)
#undef ERROR
#define ERROR(...) do{fprintf(stderr,__VA_ARGS__);fputc('\n',stderr);}while(0)
void Check(bool ok,const char* message){if(!ok){ERROR("FAIL: %s",message);exit(1);}}
namespace {
using worldpose::Pair;
worldpose::Exact poses;
Pair pair(float yaw){worldpose::Pose p{0,sinf(yaw*.5f),0,cosf(yaw*.5f),-.032f,1.7f,0};auto r=p;r.x=.032f;return {p,r};}
}
int worldpose::SelectImage(unsigned tag,Pair& result,unsigned* flags){int s=poses.Select(tag);if(s>0){result=poses.Get();if(flags)*flags=poses.Flags();}return s;}
namespace {
unsigned char engineBytes[128]{},rendererBytes[128]{};void* engine=engineBytes;void* renderer=rendererBytes;
XrSession session=reinterpret_cast<XrSession>(1);
template<class T>T at(void* p,int offset){T v;memcpy(&v,static_cast<unsigned char*>(p)+offset,sizeof(v));return v;}
double seconds(){using namespace std::chrono;return duration<double>(steady_clock::now().time_since_epoch()).count();}
GLuint vao=0,copyProgram=0,sourceFbo=0,menuFbo=0;
GLint copyDecode=0,copyScale=0,copyBounds=0;
GLuint chainTextures[3]{};unsigned acquired=0,released=0;unsigned calls=0,lastCount=0;
XrCompositionLayerProjectionView lastViews[2]{};
XrResult XRAPI_PTR mockDestroy(XrSwapchain){glDeleteTextures(3,chainTextures);memset(chainTextures,0,sizeof(chainTextures));return XR_SUCCESS;}
XrResult XRAPI_PTR mockCreate(XrSession,const XrSwapchainCreateInfo* i,XrSwapchain* out){
    glGenTextures(3,chainTextures);for(auto t:chainTextures){glBindTexture(GL_TEXTURE_2D,t);glTexStorage2D(GL_TEXTURE_2D,1,i->format,i->width,i->height);}
    *out=reinterpret_cast<XrSwapchain>(1);return XR_SUCCESS;
}
XrResult XRAPI_PTR mockEnumerate(XrSwapchain,uint32_t capacity,uint32_t* count,XrSwapchainImageBaseHeader* images){
    *count=3;if(capacity){auto* p=reinterpret_cast<XrSwapchainImageOpenGLESKHR*>(images);for(int i=0;i<3;i++)p[i].image=chainTextures[i];}return XR_SUCCESS;
}
XrResult XRAPI_PTR mockAcquire(XrSwapchain,const XrSwapchainImageAcquireInfo*,uint32_t* index){*index=++acquired%3;return XR_SUCCESS;}
XrResult XRAPI_PTR mockWait(XrSwapchain,const XrSwapchainImageWaitInfo*){return XR_SUCCESS;}
XrResult XRAPI_PTR mockRelease(XrSwapchain,const XrSwapchainImageReleaseInfo*){released=acquired%3;return XR_SUCCESS;}
XrResult XRAPI_PTR mockProc(XrInstance,const char* name,PFN_xrVoidFunction* out){
    if(strcmp(name,"xrDestroySwapchain"))return XR_ERROR_FUNCTION_UNSUPPORTED;
    *out=reinterpret_cast<PFN_xrVoidFunction>(mockDestroy);return XR_SUCCESS;
}
auto createSwapchain=mockCreate;auto enumerateImages=mockEnumerate;auto acquireImage=mockAcquire;
auto waitImage=mockWait;auto releaseImage=mockRelease;auto getProc=mockProc;
bool init(){return true;}
#include "../../android/native/cinemaglguard.inc"
#include "../../android/native/cinemacopy.inc"
bool initMenu(int,int){
    if(!sourceFbo)glGenFramebuffers(1,&sourceFbo);if(!menuFbo)glGenFramebuffers(1,&menuFbo);
    return copyProgram||initCopy();
}
bool hudHidden=false;
unsigned appendHud(const XrCompositionLayerBaseHeader**,unsigned count,bool hidden){hudHidden=hidden;return count;}
bool hudPrepared(){return false;}
int submitFrame(const XrCompositionLayerBaseHeader* const* layers,uint32_t count){
    calls++;lastCount=count;if(count){
        Check(count==1&&layers[0]->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION,"only one world projection, no stale stock layer");
        auto* layer=reinterpret_cast<const XrCompositionLayerProjection*>(layers[0]);
        Check(layer->viewCount==2,"two eyes");for(int i=0;i<2;i++)lastViews[i]=layer->views[i];
    }return 2;
}
#include "../../android/native/worldpresenter.inc"
int linearByte(int value){float x=value/255.f;return int((x<=.04045f?x/12.92f:powf((x+.055f)/1.055f,2.4f))*255+.5f);}
void Run(int width,int height,GLint encoding){
    poses.Epoch(1);GLuint source;glGenTextures(1,&source);glBindTexture(GL_TEXTURE_2D,source);
    glTexStorage2D(GL_TEXTURE_2D,1,encoding==GL_SRGB?GL_SRGB8_ALPHA8:GL_RGBA8,width,height);
    std::vector<unsigned char> pixels(size_t(width)*height*4);std::map<unsigned,int> reds;
    auto upload=[&](unsigned tag,int red,bool map){
        glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
        for(size_t i=0;i<pixels.size();i+=4){pixels[i]=red;pixels[i+1]=64;pixels[i+2]=128;pixels[i+3]=255;}
        pixels[0]=tag>>8;pixels[1]=4;pixels[2]=tag&255;
        glBindTexture(GL_TEXTURE_2D,source);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        reds[tag]=red;if(map)poses.Map(tag,poses.Record(pair((tag%1000)*.001f),seconds()),tag&1);
    };
    GLuint callerPbo;glGenBuffers(1,&callerPbo);glBindBuffer(GL_PIXEL_PACK_BUFFER,callerPbo);glBufferData(GL_PIXEL_PACK_BUFFER,64,nullptr,GL_STREAM_READ);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    unsigned submitted=0;
    for(unsigned frame=1;frame<=40;frame++){
        unsigned tag=60000+frame;upload(tag,64+frame,true);
        glPixelStorei(GL_PACK_ALIGNMENT,8);glPixelStorei(GL_PACK_ROW_LENGTH,17);glPixelStorei(GL_PACK_SKIP_PIXELS,2);glPixelStorei(GL_PACK_SKIP_ROWS,1);
        glBindBuffer(GL_PIXEL_PACK_BUFFER,callerPbo);glPixelStorei(GL_UNPACK_ROW_LENGTH,19);
        if(copySrgbControl)glDisable(0x8db9);
        Check(drawWorld(source,width,height,tag-1)==2,"production world frame consumes exactly one XR frame"); // Deliberately WRONG CPU tag.
        if(copySrgbControl)Check(!glIsEnabled(0x8db9),"world copies restore the caller's sRGB write control");
        GLint row,skip,alignment,pbo,unpackRow;glGetIntegerv(GL_PACK_ROW_LENGTH,&row);glGetIntegerv(GL_PACK_SKIP_PIXELS,&skip);glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pbo);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&unpackRow);
        Check(row==17&&skip==2&&alignment==8&&pbo==GLint(callerPbo)&&unpackRow==19,"caller's PBO and transfer state restored");
        glFinish(); // Test driver only: production fence polling never waits.
        if(lastCount){
            submitted++;auto shown=worldOutput.slots[worldOutput.shown].tag;
            Check(hudHidden==bool(shown&1),"HUD handoff belongs to the immutable GPU world image, not the newer CPU frame");
            auto expected=pair((shown%1000)*.001f);
            Check(!memcmp(&lastViews[0].pose,&expected.left,sizeof(expected.left))&&!memcmp(&lastViews[1].pose,&expected.right,sizeof(expected.right)),"submitted eyes use the GPU image's actual source pose");
            Check(lastViews[0].subImage.imageRect.extent.width==width/2&&lastViews[1].subImage.imageRect.offset.x==width/2,"uncropped full-resolution stereo rectangles");
            Check(fabsf(lastViews[0].fov.angleLeft+100*3.14159265f/360)<.00001f,"game FOV preserved");
            Check(fabsf(lastViews[0].fov.angleUp-90*3.14159265f/360)<.00001f,"vertical FOV preserved independently");
            glBindFramebuffer(GL_READ_FRAMEBUFFER,menuFbo);glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,chainTextures[released],0);
            glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glPixelStorei(GL_PACK_ALIGNMENT,4);glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);
            unsigned char color[4];glReadPixels(width/4,height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,color);
            Check(abs(int(color[0])-reds[shown])<=2,"sRGB compositor image retains color precision and the GPU tag's source image despite lagged CPU metadata");
        }
    }
    Check(submitted>=35,"async snapshots make progress");
    // Drain the last producer snapshot before measuring steady-state repeats.
    Check(drawWorld(source,width,height,60039)==2,"last in-flight image drains");glFinish();
    Check(worldOutput.slots[worldOutput.shown].tag==60040,"newest completed GPU image is selected");
    unsigned copies=worldOutput.copied;auto held=lastViews[0].pose;
    for(int i=0;i<10;i++){Check(drawWorld(source,width,height,60039)==2,"held frame");glFinish();}
    Check(worldOutput.copied==copies&&!memcmp(&held,&lastViews[0].pose,sizeof(held)),"repeated image needs no extra GPU copy and retains its source pose");
    // Lost mappings must not permanently fill all three slots.
    for(unsigned i=0;i<8;i++){upload(61000+i,128,false);drawWorld(source,width,height,61000+i);glFinish();}
    upload(62000,160,true);for(int i=0;i<5;i++){drawWorld(source,width,height,62000);glFinish();}
    Check(lastCount==1&&worldOutput.slots[worldOutput.shown].tag==62000,"queue recovers after missing UDP mapping");
    poses.Epoch(2);drawWorld(source,width,height,62000);glFinish();Check(lastCount==0,"old reference-space image cannot be submitted after wake");
    upload(62001,180,true);for(int i=0;i<5;i++){drawWorld(source,width,height,62001);glFinish();}
    Check(lastCount==1&&worldOutput.slots[worldOutput.shown].tag==62001,"fresh image restores presentation after epoch change");
    resetWorld(true);glDeleteTextures(1,&source);glDeleteBuffers(1,&callerPbo);Check(glGetError()==GL_NO_ERROR,"clean GL state after release");
    printf("PASS: production GPU world %dx%d encoding=%x, atomic RGB/tag/source pose, PBO states, no-copy repeats, dropped packet recovery, sleep epoch\n",width,height,encoding);
}
}
int main(){
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);Check(display!=EGL_NO_DISPLAY,"EGL display");
    EGLint major,minor;Check(eglInitialize(display,&major,&minor),"EGL/ANGLE initialized");
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count;Check(eglChooseConfig(display,attrs,&config,1,&count)&&count,"GLES3 configuration");
    EGLint size[]={EGL_WIDTH,2,EGL_HEIGHT,2,EGL_NONE},contextAttrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(display,config,size);EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttrs);
    Check(surface!=EGL_NO_SURFACE&&context!=EGL_NO_CONTEXT&&eglMakeCurrent(display,surface,surface,context),"hidden GLES3 pbuffer");
    printf("GLES: %s\n",glGetString(GL_RENDERER));glGenVertexArrays(1,&vao);
    copyColorFormat=GL_SRGB8_ALPHA8;
    float fovX=100,fovY=90;memcpy(rendererBytes+0x1c,&fovX,4);memcpy(rendererBytes+0x20,&fovY,4);
    Run(1024,512,GL_LINEAR);Run(1728,864,GL_SRGB);
    glDeleteVertexArrays(1,&vao);if(copyProgram)glDeleteProgram(copyProgram);if(sourceFbo)glDeleteFramebuffers(1,&sourceFbo);if(menuFbo)glDeleteFramebuffers(1,&menuFbo);
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
}
