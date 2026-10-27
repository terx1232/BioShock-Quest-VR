// Production menu/world copy shader. Hidden GPU context, no game input.
#ifdef _WIN32
#include <windows.h>
#endif
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <set>
#include <initializer_list>
#define INFO(...) do{printf(__VA_ARGS__);puts("");}while(0)
#undef ERROR
#define ERROR(...) do{fprintf(stderr,__VA_ARGS__);fputc('\n',stderr);}while(0)
void Check(bool ok,const char* m){if(!ok){ERROR("FAIL: %s",m);exit(1);}}
namespace {
GLuint vao=0,menuFbo=0,copyProgram=0;GLint copyDecode=0,copyScale=0,copyBounds=0;
#include "../../android/native/cinemacopy.inc"
std::vector<unsigned char> Copy(const std::vector<unsigned char>& pixels,int sw,int sh,int dw,int dh,GLint sourceFormat,GLint targetFormat,int quality,bool fullSource=false){
    GLuint t[2];glGenTextures(2,t);glBindTexture(GL_TEXTURE_2D,t[0]);glTexImage2D(GL_TEXTURE_2D,0,sourceFormat,sw,sh,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D,t[1]);glTexImage2D(GL_TEXTURE_2D,0,targetFormat,dw,dh,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER,menuFbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t[1],0);
    Check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"color target");
    if(copySrgbControl)glDisable(0x8db9); // Simulate a caller with sRGB writes disabled.
    copyImage(t[0],dw,dh,sourceFormat==GL_RGBA8,sw,sh,quality,fullSource);
    if(copySrgbControl)Check(!glIsEnabled(0x8db9),"caller's sRGB state restored");
    std::vector<unsigned char> result(dw*dh*4);glReadPixels(0,0,dw,dh,GL_RGBA,GL_UNSIGNED_BYTE,result.data());
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,0,0);glDeleteTextures(2,t);
    Check(glGetError()==GL_NO_ERROR,"copy GL state");return result;
}
void TravelFit(){
    for(auto dimensions:{std::pair<int,int>{1728,864},{1280,720},{720,720}}){
        int sw=dimensions.first,sh=dimensions.second,dw=1280,dh=720;
        std::vector<unsigned char> source(sw*sh*4,255);
        for(int y=0;y<sh;y++)for(int x=0;x<sw;x++){
            auto p=&source[(y*sw+x)*4];p[0]=x<sw/2?255:0;p[1]=y<sh/2?255:0;p[2]=0;
        }
        auto out=Copy(source,sw,sh,dw,dh,GL_RGBA8,GL_SRGB8_ALPHA8,1,true);
        int vw=dw,vh=dh;if(int64_t(sw)*dh>int64_t(dw)*sh)vh=int(int64_t(dw)*sh/sw);else vw=int(int64_t(dh)*sw/sh);
        int x0=(dw-vw)/2,y0=(dh-vh)/2;
        auto pixel=[&](int x,int y){return &out[(y*dw+x)*4];};
        auto tl=pixel(x0+vw/4,y0+vh*3/4),br=pixel(x0+vw*3/4,y0+vh/4);
        Check(tl[0]>250&&tl[1]>250&&br[0]<3&&br[1]<3,"loading full source, orientation and all corners retained");
        if(vh<dh)Check(pixel(dw/2,0)[0]==0&&pixel(dw/2,dh-1)[1]==0,"wide loading source letterboxed without stretching/cropping");
        if(vw<dw)Check(pixel(0,dh/2)[0]==0&&pixel(dw-1,dh/2)[1]==0,"square loading source pillarboxed");
    }
    puts("PASS: loading full-source fit 1728x864/1280x720/720x720 -> fixed 1280x720, orientation, corners and black bars");
}
void Ramps(){
    constexpr int w=1024,h=8;std::vector<unsigned char> source(w*h*4,255);
    for(int y=0;y<h;y++)for(int x=0;x<w;x++)for(int c=0;c<3;c++)source[(y*w+x)*4+c]=x/4;
    auto old=Copy(source,w,h,w,h,GL_RGBA8,GL_RGBA8,0);std::set<int> oldDark;
    for(int x=0;x<64;x++)oldDark.insert(old[x*16]);
    printf("old linear RGBA8 path: %zu of 64 near-black steps\n",oldDark.size());Check(oldDark.size()<20,"fixture reproduces near-black precision loss");
    for(GLint format:{GL_RGBA8,GL_SRGB8_ALPHA8})for(int mode:{1,3}){
        auto out=Copy(source,w,h,w,h,format,GL_SRGB8_ALPHA8,mode);std::set<int> dark,all;
        for(int x=0;x<256;x++){int value=out[x*16];Check(abs(value-x)<=1,"display-encoded grayscale preserved");all.insert(value);if(x<64)dark.insert(value);}
        Check(dark.size()>=63&&all.size()>=250,"smooth dark and full tonal ramps");
        printf("sRGB output format=%x AA=%d: dark %zu/64, full %zu/256\n",format,mode,dark.size(),all.size());
    }
}
void Edges(){
    constexpr int w=256,h=128;std::vector<unsigned char> source(w*h*4,255);
    for(int y=0;y<h;y++)for(int x=0;x<w;x++){int v=y<x*.37f+10.1f?16:96;for(int c=0;c<3;c++)source[(y*w+x)*4+c]=v;}
    auto raw=Copy(source,w,h,w,h,GL_RGBA8,GL_SRGB8_ALPHA8,0),aa=Copy(source,w,h,w,h,GL_RGBA8,GL_SRGB8_ALPHA8,1);
    unsigned rawSoft=0,aaSoft=0,changed=0;
    for(int i=0;i<w*h;i++){int a=raw[i*4],b=aa[i*4];rawSoft+=a>19&&a<93;aaSoft+=b>19&&b<93;changed+=abs(a-b)>2;}
    Check(rawSoft==0&&aaSoft>80,"diagonal shadow staircase gains subpixel edge coverage");Check(changed<unsigned(w*h/12),"smoothing is local to edges, not a scene blur");
    printf("diagonal edge: partial-coverage pixels %u -> %u, changed %u/%u\n",rawSoft,aaSoft,changed,w*h);
    // Bright native UI strokes must not become another casualty of shadow AA.
    for(int channel:{-1,0,1,2}){
        for(int y=0;y<h;y++)for(int x=0;x<w;x++){
            bool glyph=x>80&&x<120&&y>30&&y<90&&((x-80)%8<2||(y-30)%20<2);
            int i=(y*w+x)*4;
            for(int c=0;c<3;c++)source[i+c]=glyph?(channel<0||channel==c?255:0):16;
        }
        auto textRaw=Copy(source,w,h,w,h,GL_RGBA8,GL_SRGB8_ALPHA8,0),textAA=Copy(source,w,h,w,h,GL_RGBA8,GL_SRGB8_ALPHA8,1);
        for(size_t i=0;i<textRaw.size();i++)Check(abs(int(textRaw[i])-int(textAA[i]))<=1,"white/colored UI thin strokes are protected from shadow smoothing");
    }
    // A color seam between eyes must not be mistaken for an image edge.
    for(int y=0;y<h;y++)for(int x=0;x<w;x++){int i=(y*w+x)*4;source[i]=x<w/2?80:0;source[i+1]=x<w/2?0:160;source[i+2]=0;}
    auto stereo=Copy(source,w,h,w,h,GL_RGBA8,GL_SRGB8_ALPHA8,3);
    for(int y=0;y<h;y++)for(int x=w/2-4;x<w/2+4;x++){int i=(y*w+x)*4;Check(abs(int(stereo[i])-(x<w/2?80:0))<=1&&abs(int(stereo[i+1])-(x<w/2?0:160))<=1,"left/right images never mix at stereo seam");}
    // Padded world pixels must not enter a cropped 1280x720-style menu.
    for(int y=0;y<h;y++)for(int x=0;x<w;x++){int i=(y*w+x)*4;source[i]=255;source[i+1]=0;source[i+2]=255;if(x<w/2&&y<h/2)source[i]=source[i+1]=source[i+2]=64;}
    auto menu=Copy(source,w,h,w/2,h/2,GL_RGBA8,GL_SRGB8_ALPHA8,1);
    for(size_t i=0;i<menu.size();i+=4)Check(abs(int(menu[i])-64)<=1&&abs(int(menu[i+1])-64)<=1&&abs(int(menu[i+2])-64)<=1,"AA preserves the complete cropped menu without sampling padding");
}
}
int main(){
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);EGLint major,minor;Check(eglInitialize(display,&major,&minor),"EGL");
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count;Check(eglChooseConfig(display,attrs,&config,1,&count)&&count,"GLES3 config");
    EGLint size[]={EGL_WIDTH,2,EGL_HEIGHT,2,EGL_NONE},ca[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};EGLSurface surface=eglCreatePbufferSurface(display,config,size);EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,ca);
    Check(eglMakeCurrent(display,surface,surface,context),"hidden context");printf("GLES renderer: %s\n",glGetString(GL_RENDERER));glGenVertexArrays(1,&vao);glGenFramebuffers(1,&menuFbo);glDisable(GL_DITHER);
    copyColorFormat=GL_SRGB8_ALPHA8;Check(initCopy(),"production copy/color self-tests");Ramps();Edges();TravelFit();
    glDeleteProgram(copyProgram);glDeleteVertexArrays(1,&vao);glDeleteFramebuffers(1,&menuFbo);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
    puts("PASS: shadow tonal precision, spatial edge AA, eye isolation, cropped menu, sRGB write state");
}
