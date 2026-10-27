// Separate GPU-only HUD window. RGB and coverage travel separately because
// X11/Wine presentation may replace a window's alpha with opaque coverage.
#include "hudbridge.h"
#include "d3dguard.h"
#include "log.h"
#include <windows.h>
#include <d3d9.h>
namespace {
#include "stereoshaders.inc"
IDirect3DDevice9* owner=nullptr;IDirect3DSwapChain9* chain=nullptr;
IDirect3DPixelShader9* coverage=nullptr;IDirect3DStateBlock9* state=nullptr;
HWND window=nullptr;unsigned width=0,height=0,sequence=0;bool failed=false;
LRESULT CALLBACK TransportProc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_NCHITTEST)return HTTRANSPARENT;if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;return DefWindowProcW(h,m,w,l);}
bool Ensure(IDirect3DDevice9* d,const hudlayout::Source& s){
    if(failed)return false;
    if(owner==d&&chain&&width==s.width&&height==s.height)return true;
    hudbridge::Release();owner=d;width=s.width;height=s.height;
    // Odd height is a dedicated transport signature, never a world/menu target.
    window=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TRANSPARENT,L"STATIC",L"BioShockVR HUD GPU",WS_POPUP,
        GetSystemMetrics(SM_CXSCREEN)-1,GetSystemMetrics(SM_CYSCREEN)-1,width,height*2+1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(window)SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(TransportProc));
    D3DPRESENT_PARAMETERS p{};p.BackBufferWidth=width;p.BackBufferHeight=height*2+1;p.BackBufferFormat=D3DFMT_A8R8G8B8;
    p.BackBufferCount=1;p.MultiSampleType=D3DMULTISAMPLE_NONE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=window;
    p.Windowed=TRUE;p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    if(!window||FAILED(d->CreateAdditionalSwapChain(&p,&chain))||FAILED(d->CreatePixelShader(kHudpackPS,&coverage))||FAILED(d->CreateStateBlock(D3DSBT_ALL,&state))){
        Log("Native HUD: transport unavailable; legacy HUD retained");hudbridge::Release();failed=true;return false;
    }
    ShowWindow(window,SW_SHOWNOACTIVATE);
    Log("Native HUD: GPU transport %ux%u (RGB + coverage), no focus or readback",width,height*2+1);return true;
}
}
namespace hudbridge {
void Release(){if(state)state->Release();if(coverage)coverage->Release();if(chain)chain->Release();if(window)DestroyWindow(window);
    state=nullptr;coverage=nullptr;chain=nullptr;window=nullptr;owner=nullptr;width=height=0;}
bool Present(IDirect3DDevice9* d,IDirect3DTexture9* tex,const hudlayout::Source& s){
    if(!tex||!s.Valid()||!Ensure(d,s))return false;
    TargetsGuard targets(d);if(FAILED(state->Capture()))return false;
    IDirect3DSurface9 *back=nullptr,*source=nullptr;
    bool ok=SUCCEEDED(chain->GetBackBuffer(0,D3DBACKBUFFER_TYPE_MONO,&back))&&SUCCEEDED(tex->GetSurfaceLevel(0,&source));
    if(ok){
        d->SetDepthStencilSurface(nullptr);for(unsigned i=0;i<targets.count;i++)d->SetRenderTarget(i+1,nullptr);
        d->SetRenderTarget(0,back);
        RECT from{LONG(s.x),LONG(s.y),LONG(s.x+s.width),LONG(s.y+s.height)},to{0,0,LONG(width),LONG(height)};
        ok=SUCCEEDED(d->StretchRect(source,&from,back,&to,D3DTEXF_NONE));
        d->SetVertexShader(nullptr);d->SetPixelShader(coverage);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
        d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_STENCILENABLE,FALSE);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
        d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
        d->SetRenderState(D3DRS_CLIPPLANEENABLE,0);d->SetRenderState(D3DRS_VERTEXBLEND,D3DVBF_DISABLE);d->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE,FALSE);
        d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);d->SetRenderState(D3DRS_FOGENABLE,FALSE);d->SetRenderState(D3DRS_LIGHTING,FALSE);
        d->SetTexture(0,tex);d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
        d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
        float region[]={float(s.x)/s.textureWidth,float(s.y)/s.textureHeight,float(width)/s.textureWidth,float(height)/s.textureHeight};d->SetPixelShaderConstantF(0,region,1);
        D3DVIEWPORT9 vp{0,height,width,height,0,1};d->SetViewport(&vp);
        struct V{float x,y,z,w,u,v;};V q[]={{-.5f,float(height)-.5f,0,1,0,0},{float(width)-.5f,float(height)-.5f,0,1,1,0},
            {-.5f,float(height*2)-.5f,0,1,0,1},{float(width)-.5f,float(height*2)-.5f,0,1,1,1}};
        if(SUCCEEDED(d->BeginScene())){ok=SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V)))&&ok;d->EndScene();}else ok=false;
        unsigned tag=++sequence&65535;RECT pixel{0,0,1,1};ok=SUCCEEDED(d->ColorFill(back,&pixel,D3DCOLOR_ARGB(255,tag>>8,5,tag&255)))&&ok;
    }
    if(source)source->Release();if(back)back->Release();state->Apply();
    if(ok){HRESULT shown=chain->Present(nullptr,nullptr,nullptr,nullptr,D3DPRESENT_DONOTWAIT);ok=SUCCEEDED(shown)||shown==D3DERR_WASSTILLDRAWING;}
    return ok;
}
}
