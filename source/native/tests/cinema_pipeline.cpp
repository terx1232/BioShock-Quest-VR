// GPU check of stereo cinema, pointer input and head motion without a headset.
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <vector>
#include "../src/config.h"
#include "../src/theater.h"
#include "../src/vrframe.h"
#include "../src/controllermodel.h"
Config g_config;
xrapi::State pose{};
bool pointerActive=false,clicked=false;
int pointerX=0,pointerY=0,models=0;
IDirect3DSurface9* extraTarget=nullptr;
bool nativeReady=false;
int sentCinemaMode=-1;
unsigned sentWidth=0,sentHeight=0;
namespace xrapi {
bool NativeCinemaReady(){return nativeReady;}
void SendCinema(int mode,vr::Vec3,vr::Vec3,vr::Vec3,vr::Vec3,float,float,int,unsigned w,unsigned h){sentCinemaMode=mode;sentWidth=w;sentHeight=h;}
}
void Check(bool condition,const char* detail) { if(!condition){fprintf(stderr,"FAIL: %s\n",detail);exit(1);} }
void Log(const char* format,...) { va_list a;va_start(a,format);vprintf(format,a);va_end(a);puts(""); }
namespace camera { unsigned ViewportHeight(){return 1536;} }
namespace stereo { bool Enabled(){return true;} bool Ready(){return false;} bool SplitActive(){return false;} }
namespace vrframe { Frame Current(){Frame f{};f.active=true;return f;} xrapi::State State(){return pose;} }
namespace mouse {
void Point(bool active,int x,int y,bool button){pointerActive=active;pointerX=x;pointerY=y;clicked=button;}
void Escape(bool){} void Resync(){} void SetScreenSize(int,int){}
}
namespace devharness { bool Point(float&,float&){return false;} }
namespace hud {
bool Ready(){return false;} IDirect3DTexture9* Texture(){return nullptr;}
const float* LeftArea(){static float a[4]={0,0,.36f,.26f};return a;}
const float* RightArea(){static float a[4]={.7f,.76f,1,1};return a;}
}
namespace controllermodel {
const Vertex* Get(bool,unsigned& count){++models;count=0;return nullptr;}
const Vertex* PropModel(bool,unsigned& count){++models;count=0;return nullptr;}
}
float Draw(IDirect3DDevice9* device,IDirect3DSurface9* back,IDirect3DSurface9* readback) {
    Check(SUCCEEDED(device->ColorFill(back,nullptr,0xff204080)),"source menu color");
    Check(theater::Render(device,100,false),"render stereo cinema");
    Check(SUCCEEDED(device->GetRenderTargetData(back,readback)),"read cinema frame");
    D3DLOCKED_RECT map{};Check(SUCCEEDED(readback->LockRect(&map,nullptr,D3DLOCK_READONLY)),"lock cinema");
    unsigned blue[2]={},white[2]={};double center[2]={};
    for(unsigned y=0;y<1536;++y){auto row=(DWORD*)((BYTE*)map.pBits+y*map.Pitch);
        for(unsigned x=0;x<3072;++x){unsigned c=row[x];unsigned r=(c>>16)&255,g=(c>>8)&255,b=c&255;unsigned eye=x/1536;
            if(b>80 && r<80 && g<100){++blue[eye];center[eye]+=x%1536;}
            if(r>240 && g>240 && b>240)++white[eye];
        }
    }
    readback->UnlockRect();
    Check(blue[0]>100000 && blue[1]>100000,"sharp menu screen present in both eyes");
    Check(white[0]>1200 && white[1]>1200,"white beam keeps visible coverage in both eyes");
    Check(pointerActive && clicked && pointerX>0 && pointerX<3072 && pointerY>0 && pointerY<1536,"ray drives the game cursor and trigger click");
    Check(models==0,"controller/character models absent from cinema");
    D3DVIEWPORT9 vp{};device->GetViewport(&vp);
    Check(vp.Width==3072 && vp.Height==1536,"game viewport restored");
    if (extraTarget) {
        IDirect3DSurface9* actual=nullptr;
        Check(SUCCEEDED(device->GetRenderTarget(1,&actual)) && actual==extraTarget,"additional game target survives cinema");
        actual->Release();
    }
    printf("cinema blue=%u/%u white=%u/%u cursor=%d,%d\n",blue[0],blue[1],white[0],white[1],pointerX,pointerY);
    return (float)(center[0]/blue[0]);
}
int main(){
    g_config.flatMenus=false;g_config.menuStereo=true;g_config.screenMipmaps=false;g_config.menuHands=false;g_config.menuProps=false;
    g_config.theaterDraw=true;g_config.screenDistance=3.5f;g_config.screenWidthDeg=65;g_config.mouseInject=true;
    pose.valid=true;pose.hmdOrientation.w=1;pose.hmdPosition={0,1.7f,0};pose.ipd=.063f;
    pose.right.orientation.w=pose.right.grip.w=1;pose.right.position={.2f,1.3f,-.2f};pose.buttons[xrapi::R_TRIGGER]=true;
    HWND window=CreateWindowExW(0,L"STATIC",L"Cinema test",WS_OVERLAPPED,0,0,3072,1536,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    IDirect3D9* api=Direct3DCreate9(D3D_SDK_VERSION);Check(api!=nullptr,"D3D9");
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=3072;pp.BackBufferHeight=1536;
    pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* device=nullptr;
    Check(SUCCEEDED(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device)),"cinema device");
    IDirect3DSurface9* back=nullptr;device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back);
    IDirect3DSurface9* readback=nullptr;
    Check(SUCCEEDED(device->CreateOffscreenPlainSurface(3072,1536,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr)),"readback surface");
    D3DCAPS9 caps{};device->GetDeviceCaps(&caps);
    if (caps.NumSimultaneousRTs>1) {
        Check(SUCCEEDED(device->CreateRenderTarget(3072,1536,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&extraTarget,nullptr)),"additional target");
        Check(SUCCEEDED(device->SetRenderTarget(1,extraTarget)),"bind additional target");
    }
    float first=Draw(device,back,readback);
    pose.hmdOrientation={0,.08f,0,.996795f};
    float turned=Draw(device,back,readback);
    Check(fabsf(first-turned)>40,"world-locked screen reacts to head rotation");
    for (float yaw : {-.24f,-.16f,-.08f,0.0f,.08f,.16f,.24f}) {
        pose.hmdOrientation={0,sinf(yaw*.5f),0,cosf(yaw*.5f)};
        Draw(device,back,readback);
    }
    g_config.nativeCinema=true;
    for(bool acknowledged : {false,true,false}){
    nativeReady=acknowledged;
    Check(SUCCEEDED(device->ColorFill(back,nullptr,0xff204080)),"raw native source");
    Check(theater::Render(device,100,false)&&sentCinemaMode==2,"acknowledged native cinema handoff");
    Check(sentWidth==3072&&sentHeight==1536,"explicit native panel dimensions match client image");
    Check(SUCCEEDED(device->GetRenderTargetData(back,readback)),"native source readback");
    D3DLOCKED_RECT raw{};readback->LockRect(&raw,nullptr,D3DLOCK_READONLY);
    Check(*(DWORD*)raw.pBits==0xff204080 && *((DWORD*)raw.pBits+3071)==0xff204080,"native quad receives the full original menu");
    readback->UnlockRect();
    }
    Check(theater::Render(device,100,false,true,1280,720)&&sentCinemaMode==3,"detached travel gets an independent loading quad");
    Check(sentWidth==1280&&sentHeight==720,"loading quad stays 1280x720 while physical device retains larger world size");
    Check(SUCCEEDED(device->GetRenderTargetData(back,readback)),"unchanged loading GPU image");
    D3DLOCKED_RECT travel{};readback->LockRect(&travel,nullptr,D3DLOCK_READONLY);
    Check(*(DWORD*)travel.pBits==0xff204080&&*((DWORD*)travel.pBits+3071)==0xff204080,"travel transport retains full raw source for Android fit, no nested cinema");
    readback->UnlockRect();
    g_config.nativeCinema=false;
    nativeReady=false;
    Draw(device,back,readback);
    if (extraTarget) {device->SetRenderTarget(1,nullptr);extraTarget->Release();}
    theater::Release();back->Release();readback->Release();device->Release();api->Release();DestroyWindow(window);
    puts("PASS: 3072x1536 stereo cinema, white ray, cursor click, no models, head rotation, viewport");return 0;
}
