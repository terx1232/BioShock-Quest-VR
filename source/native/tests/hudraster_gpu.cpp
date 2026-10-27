// Exercise the production target redirect, supersampled raster and restoration.
// Hidden host window only. No game or headset input.
#include <cstdio>
#include <cstdlib>
#include "../src/hud.cpp"
Config g_config;
void Log(const char*,...){}
namespace engineviewport {unsigned Width(){return 320;}unsigned Height(){return 180;}}
// Unused game hooks cannot be reached in this fixture. Raster/target calls
// below execute the real production implementation on the host D3D9 device.
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){abort();}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID){abort();}
namespace uobject {
int PropertyOffset(void*,const wchar_t*){abort();}
bool BoolProperty(void*,const wchar_t*,int&,unsigned&){abort();}
const wchar_t* ClassName(void*){abort();}
}
namespace stereo {bool Ready(){abort();}bool FirstPersonPass(){abort();}}
namespace worldresolution {bool WorldReady(){abort();}}
namespace navigation {bool GeometrySelected(){abort();}State Current(){abort();}}
namespace weapon {bool WrenchGeometrySelected(){abort();}bool PistolGeometrySelected(){abort();}bool GeometryMesh(unsigned,unsigned){abort();}}
namespace weaponlayer {
bool DrawPlasmidHand(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT,DrawIndexedFn){abort();}
bool DrawIndexed(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT,DrawIndexedFn,float){abort();}
bool DrawPrimitive(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT,DrawPrimitiveFn,float){abort();}
void ReplayPostProcess(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT,DrawUPFn){abort();}
}
namespace plasmid {bool IsHandsMesh(unsigned,unsigned){abort();}bool HandGeometry(unsigned,unsigned){abort();}}
HRESULT STDMETHODCALLTYPE SetTarget(IDirect3DDevice9* d,DWORD i,IDirect3DSurface9* s){return d->SetRenderTarget(i,s);}
void Check(bool v,const char* s){if(!v){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
int main(){
    HWND w=CreateWindowExW(0,L"STATIC",L"Hidden HUD supersample fixture",WS_OVERLAPPED,0,0,320,180,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    auto api=Direct3DCreate9(D3D_SDK_VERSION);Check(api!=nullptr,"D3D9");D3DPRESENT_PARAMETERS p{};
    p.Windowed=TRUE;p.hDeviceWindow=w;p.BackBufferWidth=320;p.BackBufferHeight=180;p.BackBufferFormat=D3DFMT_A8R8G8B8;
    p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.EnableAutoDepthStencil=TRUE;p.AutoDepthStencilFormat=D3DFMT_D24S8;
    IDirect3DDevice9* d=nullptr;Check(SUCCEEDED(api->CreateDevice(0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&d)),"hidden device");
    IDirect3DSurface9 *original=nullptr,*back=nullptr;d->GetDepthStencilSurface(&original);d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back);
    g_config.gameplayHud=true;g_config.hudSupersample=2;hud::g_device=d;hud::g_setRT=SetTarget;hud::g_active=1;
    D3DVIEWPORT9 vp{16,8,128,72,0,1};RECT sc{16,8,80,44};d->SetViewport(&vp);d->SetScissorRect(&sc);
    hud::Redirect(true);Check(hud::g_redirect&&hud::g_w==640&&hud::g_h==360,"actual HUD redirect allocates 2x UI only");
    D3DSURFACE_DESC desc{};hud::g_hudDepth->GetDesc(&desc);Check(desc.Width==640&&desc.Height==360,"independent stencil matches high-resolution UI");
    Check(hud::g_pendingSource.x==32&&hud::g_pendingSource.y==16&&hud::g_pendingSource.width==256&&hud::g_pendingSource.height==144,"scaled source includes real viewport origin");
    d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    d->SetRenderState(D3DRS_LIGHTING,FALSE);d->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE);d->SetTexture(0,nullptr);
    d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    D3DMATRIX identity={{{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}}};
    d->SetTransform(D3DTS_WORLD,&identity);d->SetTransform(D3DTS_VIEW,&identity);d->SetTransform(D3DTS_PROJECTION,&identity);
    struct V{float x,y,z;DWORD c;};V q[]={{-1,1,.5f,0xffff0000},{1,1,.5f,0xffff0000},{-1,-1,.5f,0xffff0000},{1,-1,.5f,0xffff0000}};
    d->BeginScene();{HudRaster raster(d,true,hud::g_rasterScale);Check(SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V))),"clip-space UI draw");}d->EndScene();
    D3DVIEWPORT9 restored{};RECT restoredSc{};d->GetViewport(&restored);d->GetScissorRect(&restoredSc);
    Check(!memcmp(&vp,&restored,sizeof(vp))&&!memcmp(&sc,&restoredSc,sizeof(sc)),"native cached viewport/scissor restored immediately");
    hud::Redirect(false);IDirect3DSurface9 *depth=nullptr,*target=nullptr;d->GetDepthStencilSurface(&depth);d->GetRenderTarget(0,&target);
    Check(depth==original&&target==back,"actual redirect restores original world target/depth");depth->Release();target->Release();
    IDirect3DSurface9* read=nullptr;d->CreateOffscreenPlainSurface(640,360,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr);
    Check(SUCCEEDED(d->GetRenderTargetData(hud::g_surf,read)),"supersampled GPU readback");D3DLOCKED_RECT lr{};read->LockRect(&lr,nullptr,D3DLOCK_READONLY);unsigned red=0;
    for(unsigned y=0;y<360;y++)for(unsigned x=0;x<640;x++){
        bool colored=(((DWORD*)((BYTE*)lr.pBits+y*lr.Pitch))[x]&0xffffff)==0xff0000;
        if(colored)red++;Check(colored==(x>=32&&x<160&&y>=16&&y<88),"raster/scissor coverage doubles exactly without world enlargement");
    }
    Check(red==128*72,"all UI source pixels rendered at 2x coverage");read->UnlockRect();read->Release();
    hud::Release();original->Release();back->Release();Check(SUCCEEDED(d->Reset(&p)),"Reset releases supersampled UI resources");d->Release();api->Release();DestroyWindow(w);
    puts("PASS: production HUD redirect, 2x GPU raster/scissor/source origin, independent stencil, world target/depth/viewport restoration and Reset");
}
