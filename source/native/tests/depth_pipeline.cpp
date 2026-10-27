// Offscreen GPU regression: actual embedded D3D9 shaders and stereo.cpp.
// No game hooks, headset, displayed window, or user input required.
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <vector>
#include "../src/config.h"
#include "../src/stereo.h"
#include "../src/renderpolicy.h"
#include "../src/vrframe.h"

Config g_config;
namespace theater { volatile bool g_modDraw = false; }
namespace vrframe {Frame Current(){Frame f{};f.fov=100;f.ipdUnits=4.851f;return f;}}
namespace pcvr {
bool Enabled() {
#ifdef PCVR_DEPTH_TEST
    return true;
#else
    return false;
#endif
}
}
void Log(const char* fmt, ...) {
    va_list args; va_start(args, fmt); vprintf(fmt, args); va_end(args); puts("");
}
void Check(bool ok, const char* what) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); exit(1); }
}
void HR(HRESULT hr, const char* what) {
    if (FAILED(hr)) { fprintf(stderr, "FAIL: %s (0x%08lx)\n", what, hr); exit(1); }
}
void Rect(IDirect3DDevice9* d, float x0, float x1, unsigned side, float z, D3DCOLOR color) {
    struct V { float x, y, z, w; D3DCOLOR color; };
    V v[4] = {{x0-0.5f, -0.5f, z, 1, color}, {x1-0.5f, -0.5f, z, 1, color},
              {x0-0.5f, side-0.5f, z, 1, color}, {x1-0.5f, side-0.5f, z, 1, color}};
    HR(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(V)), "draw source rect");
}
void CheckTargets(IDirect3DDevice9* d, IDirect3DSurface9* back, IDirect3DSurface9* depth,
                  IDirect3DSurface9* extra, const D3DVIEWPORT9& expected) {
    IDirect3DSurface9* rt = nullptr; IDirect3DSurface9* ds = nullptr;
    HR(d->GetRenderTarget(0, &rt), "get restored RT");
    HR(d->GetDepthStencilSurface(&ds), "get restored depth");
    Check(rt == back && ds == depth, "restore render target and depth");
    rt->Release(); ds->Release();
    if (extra) {
        HR(d->GetRenderTarget(1, &rt), "get restored MRT");
        Check(rt == extra, "restore additional render target"); rt->Release();
    }
    D3DVIEWPORT9 vp = {}; d->GetViewport(&vp);
    Check(vp.X == expected.X && vp.Y == expected.Y && vp.Width == expected.Width &&
          vp.Height == expected.Height, "restore active viewport");
}
void SaveBmp(const char* path, const std::vector<DWORD>& pixels, unsigned width = 256, unsigned height = 128) {
    BITMAPFILEHEADER file = {}; BITMAPINFOHEADER info = {};
    file.bfType = 0x4d42; file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + (DWORD)pixels.size() * 4;
    info.biSize = sizeof(info); info.biWidth = width; info.biHeight = -(LONG)height;
    info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB;
    FILE* f = fopen(path, "wb"); Check(f != nullptr, "open result BMP");
    fwrite(&file, sizeof(file), 1, f); fwrite(&info, sizeof(info), 1, f);
    fwrite(pixels.data(), 4, pixels.size(), f); fclose(f);
}
void CheckForegroundBoundary(IDirect3DDevice9* d){
    IDirect3DSurface9 *back=nullptr,*owned=nullptr,*other=nullptr;
    HR(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back),"boundary backbuffer");D3DSURFACE_DESC desc{};back->GetDesc(&desc);
    HR(d->GetDepthStencilSurface(&owned),"boundary owned depth");
    HR(d->CreateDepthStencilSurface(desc.Width,desc.Height,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&other,nullptr),"unrelated same-size depth");
    d->SetDepthStencilSurface(other);
    stereo::OnClear(d,D3DCLEAR_ZBUFFER,true);stereo::OnClear(d,D3DCLEAR_ZBUFFER,true);
    Check(!stereo::FirstPersonPass(),"same-size unrelated depth clears never identify the weapon pass");
    d->SetDepthStencilSurface(owned);theater::g_modDraw=true;
    stereo::OnClear(d,D3DCLEAR_ZBUFFER,true);stereo::OnClear(d,D3DCLEAR_ZBUFFER,true);theater::g_modDraw=false;
    Check(!stereo::FirstPersonPass(),"mod draws do not advance weapon boundary");
    stereo::OnClear(d,D3DCLEAR_ZBUFFER,false);stereo::OnClear(d,D3DCLEAR_TARGET,true);
    Check(!stereo::FirstPersonPass(),"menu/color clears do not advance weapon boundary");
    stereo::OnClear(d,D3DCLEAR_ZBUFFER,true);Check(!stereo::FirstPersonPass(),"world clear is not foreground");
    stereo::OnClear(d,D3DCLEAR_ZBUFFER,true);Check(stereo::FirstPersonPass(),"weapon boundary matches actual owned-depth copy");
    stereo::EndFrame();Check(!stereo::FirstPersonPass(),"next frame starts outside foreground");
    other->Release();owned->Release();back->Release();
}
void TrackingLapse(IDirect3DDevice9* d,unsigned side){
    IDirect3DSurface9* before=nullptr;d->GetDepthStencilSurface(&before);
    Check(before!=nullptr,"world depth before tracking lapse");
    for(bool tracking:{true,false,false,false,true,false,true}){
        const auto p=renderpolicy::Resolve(true,tracking,true,side,side*2,side,0);
        stereo::SetActive(d,p.worldResources);
        Check(p.width==side&&p.height==side,"headset sleep preserves the square engine viewport");
        Check(p.controls==tracking,"lost tracking disables controls without changing render mode");
        IDirect3DSurface9* after=nullptr;d->GetDepthStencilSurface(&after);
        Check(after==before&&stereo::Ready(),"tracking lapse cannot detach or recreate live world depth");
        if(after)after->Release();
    }
    const auto menu=renderpolicy::Resolve(false,false,true,side,side*2,side,0);
    Check(!menu.worldResources&&menu.width==side*2,"real menu still releases world rendering");
    before->Release();puts("PASS: tracking loss/wake preserves owned GPU depth and engine dimensions; real menu remains distinct");
}
void CheckPerEyeWorldDepth(IDirect3DDevice9* d,unsigned side){
    D3DVIEWPORT9 vp={0,0,side,side,0,1};d->SetViewport(&vp);
    stereo::OnClear(d,D3DCLEAR_ZBUFFER,true);d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0);
    d->BeginScene();d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);
    d->SetTexture(0,nullptr);d->SetRenderState(D3DRS_LIGHTING,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE,0xf);
    Rect(d,0,(float)side,side,.98f,0xff0000ff);Rect(d,side*.15f,side*.45f,side,.7f,0xffff0000);d->EndScene();
    stereo::OnClear(d,D3DCLEAR_ZBUFFER,true);d->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,1,0);
    d->BeginScene();d->SetRenderState(D3DRS_COLORWRITEENABLE,0);Rect(d,0,(float)side,side,.1f,0);d->EndScene();
    d->SetRenderState(D3DRS_COLORWRITEENABLE,0xf);
    Check(stereo::Compose(d,100,4.851f,side,side),"compose world with per-eye occlusion output");
    IDirect3DTexture9* output=stereo::WorldDepth();Check(output!=nullptr,"world depth emitted in same stereo pixels");
    IDirect3DSurface9 *surface=nullptr,*readback=nullptr;output->GetSurfaceLevel(0,&surface);
    HR(d->CreateOffscreenPlainSurface(side*2,side,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&readback,nullptr),"per-eye depth readback");
    HR(d->GetRenderTargetData(surface,readback),"read reconstructed world depth");D3DLOCKED_RECT lock{};readback->LockRect(&lock,nullptr,D3DLOCK_READONLY);
    const float* row=(const float*)((BYTE*)lock.pBits+(side/2)*lock.Pitch);
    for(unsigned eye=0;eye<2;eye++){
        Check(fabsf(row[eye*side+(unsigned)(side*.3f)]-.7f)<.00001f,"near world depth matches reconstructed color, independent of later HUD depth");
        Check(fabsf(row[eye*side+(unsigned)(side*.8f)]-.98f)<.00001f,"distant world depth preserved in both eyes");
    }
    readback->UnlockRect();surface->Release();readback->Release();stereo::EndFrame();
    Check(stereo::WorldDepth()==nullptr,"no stale world occlusion texture after frame");
    puts("PASS: per-eye world occlusion depth, Flash depth excluded, current-frame lifetime");
}
void Run(IDirect3DDevice9* d, unsigned side, const char* image, bool copyOutsideScene = false) {
    IDirect3DSurface9* back = nullptr; IDirect3DSurface9* depth = nullptr;
    HR(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back), "backbuffer");
    HR(d->GetDepthStencilSurface(&depth), "INTZ depth surface");
    D3DSURFACE_DESC frame{}; back->GetDesc(&frame);
    D3DCAPS9 caps = {}; d->GetDeviceCaps(&caps);
    IDirect3DSurface9* extra = nullptr;
    if (caps.NumSimultaneousRTs > 1) {
        HR(d->CreateRenderTarget(frame.Width, frame.Height, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &extra, nullptr), "test MRT");
        HR(d->SetRenderTarget(1, extra), "bind test MRT");
        d->SetRenderState(D3DRS_COLORWRITEENABLE1, 0);
    }
    d->ColorFill(back, nullptr, 0xffff00ff); // inactive allocation: detect stale sampling
    D3DVIEWPORT9 vp = {0, 0, side, side, 0, 1}; d->SetViewport(&vp);
    stereo::OnClear(d, D3DCLEAR_ZBUFFER, true);
    HR(d->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1, 0), "scene clear");
    HR(d->BeginScene(), "begin world");
    d->SetVertexShader(nullptr); d->SetPixelShader(nullptr);
    d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    d->SetTexture(0, nullptr);
    d->SetRenderState(D3DRS_LIGHTING, FALSE); d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_ZENABLE, TRUE); d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    Rect(d, 0, (float)side, side, 0.98f, 0xff0000ff);
    Rect(d, side*0.3f, side*0.5f, side, 0.7f, 0xffff0000);
    if (copyOutsideScene) HR(d->EndScene(), "end world before depth clear");
    // CopyDepth must work while inside a scene and preserve all game targets.
    stereo::OnClear(d, D3DCLEAR_ZBUFFER, true);
    CheckTargets(d, back, depth, extra, vp);
    Check(stereo::FirstPersonPass(), "first person depth clear detected");
    HR(d->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1, 0), "weapon clear");
    if (copyOutsideScene) HR(d->BeginScene(), "begin weapons after depth copy");
    Rect(d, side*0.7f, side*0.8f, side, 0.1f, 0xff00ff00);
    HR(d->EndScene(), "end world");
    stereo::Compose(d, 100, 4.851f, side, side);
    CheckTargets(d, back, depth, extra, vp);
    IDirect3DSurface9* sys = nullptr;
    HR(d->CreateOffscreenPlainSurface(frame.Width, frame.Height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr), "readback surface");
    HR(d->GetRenderTargetData(back, sys), "readback stereo");
    D3DLOCKED_RECT lr = {}; HR(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY), "lock stereo");
    std::vector<DWORD> pixels(frame.Width*frame.Height);
    for (unsigned y=0; y<frame.Height; y++) memcpy(&pixels[y*frame.Width], (BYTE*)lr.pBits+y*lr.Pitch, frame.Width*4);
    sys->UnlockRect(); sys->Release(); SaveBmp(image, pixels, frame.Width, frame.Height);
    float centre[2][2] = {};
    for (unsigned eye=0; eye<2; eye++) {
        int count[2] = {}, runs = 0; bool prevRed = false;
        for (unsigned x=0; x<frame.Width/2; x++) {
            DWORD c = pixels[(frame.Height/2)*frame.Width+eye*(frame.Width/2)+x];
            unsigned r = (c>>16)&255, g = (c>>8)&255, b = c&255;
            Check(!(r>200 && b>200 && g<10), "no stale magenta beyond active source rectangle");
            bool red = r>200 && g<30 && b<30, green = g>200 && r<30 && b<30;
            if (red && !prevRed) runs++;
            prevRed = red;
            if (red) { centre[eye][0] += x; count[0]++; }
            if (green) { centre[eye][1] += x; count[1]++; }
        }
        Check(runs == 1, "one foreground silhouette, no duplicated edge");
        Check(count[0] > 15 && count[1] > 5, "world and weapon visible in both eyes");
        for (int object=0; object<2; object++) centre[eye][object] /= count[object];
    }
    float world = centre[0][0]-centre[1][0], weapon = centre[0][1]-centre[1][1];
    printf("side %u: world disparity %.2f px, weapon disparity %.2f px\n", side, world, weapon);
    float scale=frame.Width/256.0f;
    Check(world>6*scale && world<10*scale, "scene depth preserved after weapon clear");
    Check(weapon>7*scale && weapon<11*scale, "weapon uses its own depth, not background depth");
    stereo::EndFrame(); Check(!stereo::FirstPersonPass(), "reset per-frame depth capture");
    if (extra) { d->SetRenderTarget(1, nullptr); extra->Release(); }
    back->Release(); depth->Release();
}

// At production source resolution a fixed 12-step search can jump completely
// over a one-pixel object. Exercise several disparities and sub-step phases.
void ThinDetails(IDirect3DDevice9* d, unsigned side) {
    const unsigned end = side-70;
    unsigned objects=0;
    for(unsigned x=70;x<end;x+=29)objects++;
    IDirect3DSurface9 *back=nullptr,*sys=nullptr;
    HR(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back),"thin backbuffer");
    const float depths[] = {0.1f,0.72f,0.82f,0.90f};
    for (float z : depths) {
        D3DVIEWPORT9 vp={0,0,side,side,0,1};d->SetViewport(&vp);
        stereo::OnClear(d,D3DCLEAR_ZBUFFER,true);
        HR(d->Clear(0,nullptr,D3DCLEAR_ZBUFFER|D3DCLEAR_TARGET,0xff0000ff,1,0),"thin clear");
        HR(d->BeginScene(),"thin begin");
        d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);
        d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);d->SetTexture(0,nullptr);
        d->SetRenderState(D3DRS_LIGHTING,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
        d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
        Rect(d,0,side,side,0.98f,0xff0000ff);
        for(unsigned x=70;x<end;x+=29)Rect(d,(float)x,(float)x+1,side,z,0xffff0000);
        HR(d->EndScene(),"thin end");
        Check(stereo::Compose(d,100,4.851f,side,side),"complete stereo pair reported ready");
        HR(d->CreateOffscreenPlainSurface(side*2,side,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&sys,nullptr),"thin readback surface");
        HR(d->GetRenderTargetData(back,sys),"thin readback");
        D3DLOCKED_RECT lr{};HR(sys->LockRect(&lr,nullptr,D3DLOCK_READONLY),"thin lock");
        const DWORD* row=(const DWORD*)((BYTE*)lr.pBits+(side/2)*lr.Pitch);
        float shift=fminf(0.035f,(4.851f*0.5f)/(2*tanf(100*3.14159265f/360))*(0.1f-0.09998474f*z))*side;
        for(unsigned eye=0;eye<2;eye++)for(unsigned x=70;x<end;x+=29){
            int expected=(int)floorf(x+(eye?-shift:shift)+0.5f);
            unsigned peak=0;
            for(int dx=-1;dx<=1;dx++){
                unsigned red=(row[eye*side+expected+dx]>>16)&255;
                if(red>peak)peak=red;
            }
            if(peak<160)fprintf(stderr,"missing thin object z=%.2f eye=%u x=%u shift=%.2f peak=%u\n",z,eye,x,shift,peak);
            Check(peak>=160,"thin foreground retained at every ray-search phase");
        }
        for(unsigned eye=0;eye<2;eye++){
            int runs=0;bool wasRed=false;
            for(unsigned x=0;x<side;x++){
                unsigned red=(row[eye*side+x]>>16)&255;
                bool isRed=red>160;
                if(isRed&&!wasRed)runs++;
                wasRed=isRed;
                if(red<40)continue;
                float distance=1000;
                for(unsigned source=70;source<end;source+=29){
                    float delta=fabsf(x-(source+(eye?-shift:shift)));
                    if(delta<distance)distance=delta;
                }
                Check(distance<=2,"no ghost stripe away from projected thin detail");
            }
            Check(runs==objects,"one silhouette per thin object in each eye");
        }
        sys->UnlockRect();sys->Release();sys=nullptr;stereo::EndFrame();
    }
    back->Release();puts("PASS: production-resolution thin silhouettes across disparities");
}

// A sloped near surface with individually coloured vertical strips. Unlike a
// single solid silhouette this exposes the old hard clamp: all close strips
// received exactly the same disparity and the surface became a flat card.
// Check the projected strip positions, continuity, and unchanged far stereo.
void NearSurface(IDirect3DDevice9* d,unsigned side) {
    g_config.depthMaxShift=.025f;g_config.depthNearSoftness=.6f;
    IDirect3DSurface9 *back=nullptr,*sys=nullptr;
    HR(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back),"near backbuffer");
    D3DVIEWPORT9 vp={0,0,side,side,0,1};d->SetViewport(&vp);
    stereo::OnClear(d,D3DCLEAR_ZBUFFER,true);
    HR(d->Clear(0,nullptr,D3DCLEAR_ZBUFFER|D3DCLEAR_TARGET,0xff000000,1,0),"near clear");
    HR(d->BeginScene(),"near begin");
    d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetTexture(0,nullptr);
    d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);
    d->SetRenderState(D3DRS_LIGHTING,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    // Each red-channel code identifies a source column, with a shallow depth
    // gradient. Leave wide gutters to avoid testing unavailable hidden content.
    unsigned start=side/3,end=start+200;
    for(unsigned x=start;x<end;x++){
        float z=.1f+.6f*(x-start)/199.f;
        Rect(d,(float)x,(float)x+1,side,z,0xff0000ff|((x-start+20)<<16));
    }
    Rect(d,80,100,side,.95f,0xff00ff00);
    HR(d->EndScene(),"near end");
    Check(stereo::Compose(d,100,4.851f,side,side),"near stereo ready");
    HR(d->CreateOffscreenPlainSurface(side*2,side,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&sys,nullptr),"near readback surface");
    HR(d->GetRenderTargetData(back,sys),"near readback");
    D3DLOCKED_RECT lr{};HR(sys->LockRect(&lr,nullptr,D3DLOCK_READONLY),"near lock");
    const DWORD* row=(const DWORD*)((BYTE*)lr.pBits+(side/2)*lr.Pitch);
    float nearDisparity[2]={};
    for(unsigned eye=0;eye<2;eye++){
        float lastSource=-1;unsigned seen=0;double farCentre=0;unsigned farCount=0;
        for(unsigned x=0;x<side;x++){
            DWORD c=row[eye*side+x];unsigned r=(c>>16)&255,g=(c>>8)&255,b=c&255;
            if(g>200&&r<20){farCentre+=x;farCount++;}
            if(b<240||r<40||r>195)continue;
            float source=start+r-20.f;
            float z=.1f+.6f*(source-start)/199.f;
            float raw=(4.851f*.5f)/(2*tanf(100*3.14159265f/360))*(.1f-.09998474f*z);
            // Numeric physical landmarks: at raw .03 the comfortable shift is
            // .01857; far values below .01 remain their original physical value.
            float expected=raw<=.01f?raw:.025f-.000225f/(raw+.005f);
            float measured=(eye?source-x:x-source)/side;
            Check(fabsf(measured-expected)<1.6f/side,"near textured surface uses smooth bounded depth");
            Check(measured>0&&measured<.025f+1.5f/side,"near disparity stays inside comfort bound");
            Check(source>=lastSource,"no folding or repeated strips on sloped near geometry");
            lastSource=source;seen++;
            if(r>=42&&r<=47)nearDisparity[eye]=measured;
        }
        Check(seen>130,"near surface remains a continuous visible silhouette");
        Check(farCount>=18,"far marker retained");
        farCentre/=farCount;
        float farShift=(4.851f*.5f)/(2*tanf(100*3.14159265f/360))*(.1f-.09998474f*.95f)*side;
        Check(fabs(farCentre-(89.5f+(eye?-farShift:farShift)))<1,"distant stereo unchanged by near knee");
    }
    printf("near surface: disparity %.2f/%.2f source pixels, smooth slope retained\n",nearDisparity[0]*side,nearDisparity[1]*side);
    sys->UnlockRect();sys->Release();back->Release();stereo::EndFrame();
    g_config.depthMaxShift=.035f;g_config.depthNearSoftness=0;
    puts("PASS: close textured geometry, bounded disparity, no folds, distant depth preserved");
}
// Exercise the game's pre-foreground copy with deterministic textured depths.
// Late FOV/strength changes deliberately require the ordinary-search fallback.
void CacheScenes(IDirect3DDevice9* d,unsigned side){
    IDirect3DSurface9 *back=nullptr;d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back);
    for(unsigned seed=0;seed<6;seed++){
        g_config.depthMaxShift=seed%2?.025f:.035f;g_config.depthNearSoftness=seed%3?.6f:0;
        g_config.depthStrength=1;
        D3DVIEWPORT9 vp={0,0,side,side,0,1};d->SetViewport(&vp);
        stereo::OnClear(d,D3DCLEAR_ZBUFFER,true);d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff204080,1,0);
        d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetTexture(0,nullptr);
        d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);d->SetRenderState(D3DRS_LIGHTING,FALSE);
        d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_ZENABLE,TRUE);
        d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,0xf);
        unsigned random=seed+12345;auto next=[&](){random=random*1664525u+1013904223u;return random;};
        d->BeginScene();Rect(d,0,side,side,.999f,0xff204080);
        for(unsigned i=0;i<100;i++){
            unsigned x=next()%side,width=1+next()%23;float z=.005f+(next()%99000)/100000.f;
            Rect(d,x,x+width>side?side:x+width,side,z,0xff000000u|(next()&0xffffff));
        }
        d->EndScene();stereo::OnClear(d,D3DCLEAR_ZBUFFER,true);d->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,1,0);
        if(seed==5)g_config.depthStrength=.75f;
        Check(stereo::Compose(d,seed==4?110.f:100.f,4.851f,side,side),"cached/fallback random scene");
        IDirect3DSurface9* sys=nullptr;HR(d->CreateOffscreenPlainSurface(side*2,side,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&sys,nullptr),"cache image readback");
        HR(d->GetRenderTargetData(back,sys),"cache color copy");D3DLOCKED_RECT lock{};sys->LockRect(&lock,nullptr,D3DLOCK_READONLY);
        std::vector<DWORD> pixels(side*side*2);for(unsigned y=0;y<side;y++)memcpy(pixels.data()+y*side*2,(BYTE*)lock.pBits+y*lock.Pitch,side*8);
        sys->UnlockRect();sys->Release();char name[80];snprintf(name,sizeof(name),"cache-%u-%u.bmp",side,seed);SaveBmp(name,pixels,side*2,side);
        IDirect3DTexture9* z=stereo::WorldDepth();Check(z!=nullptr,"cached world occlusion target");IDirect3DSurface9* surface=nullptr;z->GetSurfaceLevel(0,&surface);
        HR(d->CreateOffscreenPlainSurface(side*2,side,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&sys,nullptr),"cache depth readback");HR(d->GetRenderTargetData(surface,sys),"cache world-depth copy");
        sys->LockRect(&lock,nullptr,D3DLOCK_READONLY);snprintf(name,sizeof(name),"cache-%u-%u.depth",side,seed);FILE* f=fopen(name,"wb");Check(f!=nullptr,"cache depth artifact");
        for(unsigned y=0;y<side;y++)fwrite((BYTE*)lock.pBits+y*lock.Pitch,4,side*2,f);fclose(f);sys->UnlockRect();sys->Release();surface->Release();
        stereo::EndFrame();
    }
    back->Release();g_config.depthStrength=1;g_config.depthMaxShift=.035f;g_config.depthNearSoftness=0;
    puts("PASS: same-frame cache scenes, late FOV/strength fallback and world occlusion artifacts");
}
int main() {
    g_config.enabled = true; g_config.depthStereo = true; g_config.stereo = false;
#ifdef QUEST_LAZY_DEPTH_TEST
    g_config.lazyDepth = true;
#endif
    g_config.gameWidth = 128; g_config.gameHeight = 128; g_config.depthMaxShift = 0.035f;
    WNDCLASSA wc = {}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(nullptr); wc.lpszClassName = "DepthRegression";
    RegisterClassA(&wc);
    HWND window = CreateWindowA(wc.lpszClassName, "Offscreen test", WS_POPUP, 0, 0, 256, 128, nullptr, nullptr, wc.hInstance, nullptr);
    Check(window != nullptr, "hidden test window");
    IDirect3D9* api = Direct3DCreate9(D3D_SDK_VERSION); Check(api != nullptr, "system D3D9");
    D3DPRESENT_PARAMETERS pp = {}; pp.BackBufferWidth=256; pp.BackBufferHeight=128;
    pp.BackBufferFormat=D3DFMT_A8R8G8B8; pp.BackBufferCount=1; pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=window; pp.Windowed=TRUE; pp.EnableAutoDepthStencil=TRUE;
    pp.AutoDepthStencilFormat=D3DFMT_D24S8; pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    stereo::AdjustParams(&pp);
    IDirect3DDevice9* d = nullptr;
    HR(api->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &d), "test device");
    stereo::AfterCreate(d, &pp);
#if defined(PCVR_DEPTH_TEST) || defined(QUEST_LAZY_DEPTH_TEST)
    Check(pp.EnableAutoDepthStencil, "PC menu retains automatic depth");
    Check(!stereo::Ready(), "PC stereo starts disabled in menu");
    IDirect3DSurface9* original = nullptr; d->GetDepthStencilSurface(&original);
    D3DSURFACE_DESC menuDepth{}; original->GetDesc(&menuDepth);
    Check(menuDepth.Format == D3DFMT_D24S8, "original menu depth bound");
    Check(stereo::MapDepth(original) == original, "menu depth unmodified");
    stereo::SetActive(d, true);
    Check(stereo::MapDepth(original) != original, "game depth mapping enabled");
    original->Release();
#endif
    Check(stereo::Ready(), "depth stereo supported");
    CheckForegroundBoundary(d);
    Run(d, 128, "full.bmp"); Run(d, 96, "dynamic.bmp", true);
    // Reset must release every default-pool resource held by the mod.
    d->SetDepthStencilSurface(nullptr); stereo::Release();
    pp.EnableAutoDepthStencil=TRUE; stereo::AdjustParams(&pp);
    HR(d->Reset(&pp), "device reset after stereo release");
    stereo::AfterCreate(d, &pp);
#if defined(PCVR_DEPTH_TEST) || defined(QUEST_LAZY_DEPTH_TEST)
    stereo::SetActive(d, true);
#endif
    Check(stereo::Ready(), "stereo recreated after reset");
    Run(d, 128, "reset.bmp");
#if defined(PCVR_DEPTH_TEST) || defined(QUEST_LAZY_DEPTH_TEST)
    stereo::SetActive(d, false);
    Check(!stereo::Ready(), "PC stereo off after returning to menu");
    IDirect3DSurface9* restored = nullptr; d->GetDepthStencilSurface(&restored);
    D3DSURFACE_DESC restoredDescription{}; restored->GetDesc(&restoredDescription);
    Check(restoredDescription.Format == D3DFMT_D24S8, "menu depth restored");
    restored->Release();
#endif
    d->SetDepthStencilSurface(nullptr); stereo::Release();
    pp.BackBufferWidth=512;pp.BackBufferHeight=256;pp.EnableAutoDepthStencil=TRUE;
    stereo::AdjustParams(&pp);HR(d->Reset(&pp),"larger menu window reset");stereo::AfterCreate(d,&pp);
#if defined(PCVR_DEPTH_TEST) || defined(QUEST_LAZY_DEPTH_TEST)
    stereo::SetActive(d,true);
#endif
    Run(d,128,"upscaled.bmp");Run(d,96,"upscaled-dynamic.bmp",true);
    for(unsigned side : {720u,864u}) {
        d->SetDepthStencilSurface(nullptr);stereo::Release();
        pp.BackBufferWidth=side*2;pp.BackBufferHeight=side;pp.EnableAutoDepthStencil=TRUE;
        g_config.gameWidth=side;g_config.gameHeight=side;
        g_config.weaponGeometry=true;
        stereo::AdjustParams(&pp);HR(d->Reset(&pp),"production-resolution reset");stereo::AfterCreate(d,&pp);
#if defined(PCVR_DEPTH_TEST) || defined(QUEST_LAZY_DEPTH_TEST)
        stereo::SetActive(d,true);
#endif
        ThinDetails(d,side);
        NearSurface(d,side);
        CheckPerEyeWorldDepth(d,side);
        TrackingLapse(d,side);
        CacheScenes(d,side);
    }
    d->SetDepthStencilSurface(nullptr); stereo::Release(); d->Release(); api->Release(); DestroyWindow(window);
    puts("PASS: depth capture, weapon merge, silhouettes, dynamic UV bounds, targets, reset");
    return 0;
}
