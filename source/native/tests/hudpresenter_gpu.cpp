// Actual translucent compositor shader and panel geometry on hidden GLES.
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
#include <initializer_list>
#include "../src/hudlayout.h"
#define INFO(...) do{printf(__VA_ARGS__);puts("");}while(0)
#undef ERROR
#define ERROR(...) do{fprintf(stderr,__VA_ARGS__);fputc('\n',stderr);}while(0)
void Check(bool ok,const char* m){if(!ok){ERROR("FAIL: %s",m);exit(1);}}
namespace {
#include "../../android/native/cinemamath.inc"
unsigned char engineBytes[64]{},rendererBytes[2048]{};void* engine=engineBytes;void* renderer=rendererBytes;
float offset[3]{};float* cameraOffset=offset;XrSession session=reinterpret_cast<XrSession>(1);
template<class T>T at(void* p,int o){T v;memcpy(&v,static_cast<unsigned char*>(p)+o,sizeof(v));return v;}
GLuint vao=0,sourceFbo=0,menuFbo=0,copyProgram=0;GLint copyDecode=0,copyScale=0,copyBounds=0;
GLuint textures[3]{};unsigned current=0;bool corrupt=false;
XrResult XRAPI_PTR mockDestroy(XrSwapchain){glDeleteTextures(3,textures);return XR_SUCCESS;}
XrResult XRAPI_PTR mockCreate(XrSession,const XrSwapchainCreateInfo* i,XrSwapchain* out){glGenTextures(3,textures);for(auto t:textures){glBindTexture(GL_TEXTURE_2D,t);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,i->width,i->height);}*out=reinterpret_cast<XrSwapchain>(1);return XR_SUCCESS;}
XrResult XRAPI_PTR mockEnumerate(XrSwapchain,uint32_t capacity,uint32_t* count,XrSwapchainImageBaseHeader* images){*count=3;if(capacity){auto* p=reinterpret_cast<XrSwapchainImageOpenGLESKHR*>(images);for(int i=0;i<3;i++)p[i].image=textures[i];}return XR_SUCCESS;}
XrResult XRAPI_PTR mockAcquire(XrSwapchain,const XrSwapchainImageAcquireInfo*,uint32_t* index){*index=++current%3;return XR_SUCCESS;}
XrResult XRAPI_PTR mockWait(XrSwapchain,const XrSwapchainImageWaitInfo*){return XR_SUCCESS;}
XrResult XRAPI_PTR mockRelease(XrSwapchain,const XrSwapchainImageReleaseInfo*){return XR_SUCCESS;}
XrResult XRAPI_PTR mockProc(XrInstance,const char*,PFN_xrVoidFunction* out){*out=reinterpret_cast<PFN_xrVoidFunction>(mockDestroy);return XR_SUCCESS;}
auto createSwapchain=mockCreate;auto enumerateImages=mockEnumerate;auto acquireImage=mockAcquire;auto waitImage=mockWait;auto releaseImage=mockRelease;auto getProc=mockProc;
#include "../../android/native/cinemaglguard.inc"
#include "../../android/native/cinemacopy.inc"
#include "../../android/native/hudpresenter.inc"
void Run(GLint format){
    constexpr int width=1024,height=512,packedH=height*2+1;
    std::vector<unsigned char> packed(width*packedH*4,255);
    for(int y=0;y<height;y++)for(int x=0;x<width;x++){
        int i=(y*width+x)*4,a=((y+height)*width+x)*4;
        packed[i]=128;packed[i+1]=packed[i+2]=0;packed[a]=packed[a+1]=packed[a+2]=128;
        if(x<64){packed[i]=packed[i+1]=packed[i+2]=0;packed[a]=packed[a+1]=packed[a+2]=0;}
        if(x>=500&&x<512&&y>=180&&y<200)for(int c=0;c<3;c++)packed[i+c]=packed[a+c]=255;
    }
    GLuint source;glGenTextures(1,&source);glBindTexture(GL_TEXTURE_2D,source);glTexImage2D(GL_TEXTURE_2D,0,format,width,packedH,0,GL_RGBA,GL_UNSIGNED_BYTE,packed.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    hudInput.texture=source;hudInput.width=width;hudInput.height=packedH;hudInput.tag=1;hudInput.axesValid=true;
    hudInput.axes[43]=hudInput.axes[47]=1;hudInput.axes[9]=-.3f;hudInput.axes[21]=.3f;hudInput.axes[10]=hudInput.axes[22]=1.5f;hudInput.axes[11]=hudInput.axes[23]=-.7f;
    const XrCompositionLayerBaseHeader* layers[16]{};
    Check(appendHud(layers,0,false)==0&&hudPrepared(),"HUD readiness can be acknowledged before hiding the legacy UI");
    unsigned count=appendHud(layers,0,true);
    Check(count>=4&&count<12,"front/wrist layers preserve complete HUD");
    glBindFramebuffer(GL_READ_FRAMEBUFFER,menuFbo);glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[current%3],0);
    std::vector<unsigned char> decoded(width*height*4);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,decoded.data());
    auto pixel=[&](int x,int y){return decoded.data()+((height-1-y)*width+x)*4;};
    auto* half=pixel(100,100);Check(abs(int(half[0])-128)<=2&&abs(int(half[3])-128)<=2&&half[1]<2,"linear premultiplied color and real alpha recovered after opaque X11");
    auto* empty=pixel(10,100);Check(empty[0]<2&&empty[3]<2,"transparent HUD cannot become a black panel");
    for(int y=180;y<200;y++){auto* glyph=pixel(501,y);Check(glyph[0]>250&&glyph[3]>250,"twenty original glyph rows remain in compositor image");}
    for(int y=0;y<height;y++)for(int x=0;x<width;x++){
        int covered=0;for(unsigned i=0;i<count;i++){auto& r=reinterpret_cast<const XrCompositionLayerQuad*>(layers[i])->subImage.imageRect;
            if(x>=r.offset.x&&x<r.offset.x+r.extent.width&&y>=r.offset.y&&y<r.offset.y+r.extent.height)covered++;}
        Check(covered==1,"all original UI pixels assigned once, no cropped strings or duplicate stats");
    }
    auto* wrist=reinterpret_cast<const XrCompositionLayerQuad*>(layers[count-2]);
    Check(wrist->subImage.imageRect.offset.y==height-lroundf(height*.20f),"upper-left health crop uses GLES bottom-left image origin");
    XrVector3f old=wrist->pose.position;
    unsigned before=current;hudInput.axes[9]+=.1f;appendHud(layers,0,true);
    Check(current==before&&fabsf(reinterpret_cast<const XrCompositionLayerQuad*>(layers[count-2])->pose.position.x-old.x)>.08f,"current hand moves independently with no repeated texture copy");
    resetHud(true);glDeleteTextures(1,&source);Check(glGetError()==GL_NO_ERROR,"GPU HUD release");
    printf("PASS: real native HUD shader format=%x, original glyph resolution, alpha/color, full coverage, current hand/no-copy repeats\n",format);
}
}
int main(){
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);EGLint major,minor;Check(eglInitialize(display,&major,&minor),"EGL");
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count;Check(eglChooseConfig(display,attrs,&config,1,&count)&&count,"config");
    EGLint size[]={EGL_WIDTH,2,EGL_HEIGHT,2,EGL_NONE},ca[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(display,config,size);EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,ca);Check(eglMakeCurrent(display,surface,surface,context),"GLES3");
    glGenVertexArrays(1,&vao);glGenFramebuffers(1,&sourceFbo);glGenFramebuffers(1,&menuFbo);
    XrView views[2]{};for(auto& v:views){v.type=XR_TYPE_VIEW;v.pose.orientation.w=1;v.pose.position.y=1.7f;}XrView* viewPointer=views;
    memcpy(rendererBytes+0x630,&viewPointer,sizeof(viewPointer));float fov=100;memcpy(rendererBytes+0x1c,&fov,4);
    Run(GL_RGBA8);Run(GL_SRGB8_ALPHA8);
    glDeleteVertexArrays(1,&vao);glDeleteFramebuffers(1,&sourceFbo);glDeleteFramebuffers(1,&menuFbo);
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
}
