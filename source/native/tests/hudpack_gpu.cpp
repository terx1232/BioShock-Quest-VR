#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <vector>
#include "../src/hudbridge.cpp"
void Log(const char* f,...){va_list a;va_start(a,f);vprintf(f,a);va_end(a);puts("");}
void Check(bool ok,const char* s){if(!ok){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
int main(){
    HWND parent=CreateWindowExW(0,L"STATIC",L"hidden HUD transport fixture",WS_OVERLAPPED,0,0,1024,512,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    IDirect3D9* api=Direct3DCreate9(D3D_SDK_VERSION);D3DPRESENT_PARAMETERS p{};p.Windowed=TRUE;p.hDeviceWindow=parent;p.BackBufferWidth=1024;p.BackBufferHeight=512;p.BackBufferFormat=D3DFMT_A8R8G8B8;p.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;Check(SUCCEEDED(api->CreateDevice(0,D3DDEVTYPE_HAL,parent,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&d)),"device");
    IDirect3DTexture9* tex=nullptr;Check(SUCCEEDED(d->CreateTexture(1024,512,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&tex,nullptr)),"native HUD target");
    IDirect3DSurface9* src=nullptr;tex->GetSurfaceLevel(0,&src);d->ColorFill(src,nullptr,0x80800000);RECT white{500,180,512,200};d->ColorFill(src,&white,0xffffffff);
    IDirect3DSurface9* before=nullptr;d->GetRenderTarget(0,&before);D3DVIEWPORT9 vp{3,5,900,450,0,1};d->SetViewport(&vp);
    Check(hudbridge::Present(d,tex,{0,0,1024,512,1024,512}),"GPU window transport");
    IDirect3DSurface9* after=nullptr;d->GetRenderTarget(0,&after);Check(after==before,"game target restored");after->Release();before->Release();
    D3DVIEWPORT9 restored{};d->GetViewport(&restored);Check(!memcmp(&vp,&restored,sizeof(vp)),"game viewport restored");
    IDirect3DSurface9 *surface=nullptr,*read=nullptr;chain->GetBackBuffer(0,D3DBACKBUFFER_TYPE_MONO,&surface);d->CreateOffscreenPlainSurface(1024,1025,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr);
    Check(SUCCEEDED(d->GetRenderTargetData(surface,read)),"packed readback");D3DLOCKED_RECT map{};read->LockRect(&map,nullptr,D3DLOCK_READONLY);
    auto pixel=[&](int x,int y){return reinterpret_cast<DWORD*>(static_cast<BYTE*>(map.pBits)+y*map.Pitch)[x];};
    Check((pixel(100,100)&0xffffff)==0x800000,"premultiplied RGB preserved");Check((pixel(100,612)&0xffffff)==0x808080,"coverage survives even when X11 replaces window alpha");
    Check((pixel(0,0)&0xffffff)==0x000501,"distinct non-world GPU marker");
    for(int y=180;y<200;y++)Check((pixel(501,y)&0xffffff)==0xffffff&&(pixel(501,y+512)&0xffffff)==0xffffff,"all twenty original glyph rows reach transport");
    read->UnlockRect();read->Release();surface->Release();src->Release();tex->Release();hudbridge::Release();Check(SUCCEEDED(d->Reset(&p)),"reset after releasing extra chain");
    d->Release();api->Release();DestroyWindow(parent);puts("PASS: production HUD RGB/coverage transport, native glyph rows, isolated marker, targets/state/Reset");
}
