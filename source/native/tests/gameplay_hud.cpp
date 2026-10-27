// Production HUD renderer, hidden GPU window; no BioShock input or cursor tests.
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <vector>
#include "../src/gameplayhud.h"
#include "../src/hudcoverage.h"
void Log(const char* fmt,...){va_list a;va_start(a,fmt);vprintf(fmt,a);va_end(a);puts("");}
void Check(bool ok,const char* detail){if(!ok){fprintf(stderr,"FAIL: %s\n",detail);exit(1);}}
using hudlayout::Rect;
bool In(Rect r,float x,float y){return x>=r.x0&&x<r.x1&&y>=r.y0&&y<r.y1;}
struct Pixels{unsigned red[2]{},green[2]{},blue[2]{},magenta[2]{},half[2]{},cyan[2]{},yellow[2]{},white[2]{},gold[2]{};double rx[2]{},gx[2]{},cx[2]{},yx[2]{},wx[2]{},ax[2]{};};
int main(){
    auto src=hudlayout::Observe(1728,864,0,0,864,864);
    Check(src.Valid()&&src.width==864&&src.height==864,"canvas extent independent of SBS allocation");
    auto offset=hudlayout::Observe(2000,1000,32,16,864,864);
    auto uv=hudlayout::Uv(offset,{0,0,1,1});
    Check(offset.Valid()&&fabsf(uv.x0-.016f)<.00001f&&fabsf(uv.x1-.448f)<.00001f,"offset canvas UVs");
    Check(!hudlayout::Observe(100,100,100,0,50,50).Valid(),"reject off-texture viewport");
    Check(hudlayout::Observe(1728,864,0,0,1728,864).width==1728,"observed full Flash viewport is not the square 3D canvas");
    auto layout=hudlayout::Partition({0,0,.20f,.20f},{.80f,.76f,1,1});
    auto dashboard=hudlayout::Partition(layout.health,layout.ammo,true,true);
    for(int y=0;y<864;y++)for(int x=0;x<1728;x++){
        float u=(x+.5f)/1728,v=(y+.5f)/864;
        int count=In(dashboard.health,u,v)+In(dashboard.ammo,u,v)+In(dashboard.inventory,u,v)+In(dashboard.resources,u,v)+In(dashboard.focus,u,v);
        for(unsigned i=0;i<dashboard.count;i++)count+=In(dashboard.front[i],u,v);
        Check(count==1,"dashboard and VR text assign every source pixel exactly once");
    }
    for(int y=0;y<864;y++)for(int x=0;x<864;x++){
        float u=(x+.5f)/864,v=(y+.5f)/864;int count=In(layout.health,u,v)+In(layout.ammo,u,v);
        for(unsigned i=0;i<layout.count;i++)count+=In(layout.front[i],u,v);
        Check(count==1,"all source pixels assigned exactly once, no lost subtitles or duplicate stats");
    }
    HWND wnd=CreateWindowExW(0,L"STATIC",L"HUD GPU fixture",WS_OVERLAPPED,0,0,1728,864,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    IDirect3D9* api=Direct3DCreate9(D3D_SDK_VERSION);Check(api!=nullptr,"D3D9 available");
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=wnd;pp.BackBufferWidth=1728;pp.BackBufferHeight=864;
    pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;Check(SUCCEEDED(api->CreateDevice(0,D3DDEVTYPE_HAL,wnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d)),"hidden device");
    IDirect3DSurface9 *back=nullptr,*read=nullptr,*extra=nullptr;
    d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back);
    Check(SUCCEEDED(d->CreateOffscreenPlainSurface(1728,864,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr)),"readback");
    D3DCAPS9 caps{};d->GetDeviceCaps(&caps);
    if(caps.NumSimultaneousRTs>1){d->CreateRenderTarget(1728,864,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&extra,nullptr);d->SetRenderTarget(1,extra);}
    {
        // Exact native failure: RGB is present while alpha stays zero unless
        // the transparent target writes coverage. Exercise the production guard.
        d->SetRenderState(D3DRS_COLORWRITEENABLE,7);d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
        d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
        d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_ZENABLE,FALSE);
        d->SetRenderState(D3DRS_LIGHTING,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
        d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);d->SetTexture(0,nullptr);
        d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
        d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
        struct V{float x,y,z,w;DWORD c;};V q[]={{-.5f,-.5f,0,1,0x80ff0000},{63.5f,-.5f,0,1,0x80ff0000},{-.5f,63.5f,0,1,0x80ff0000},{63.5f,63.5f,0,1,0x80ff0000}};
        d->ColorFill(back,nullptr,0);d->BeginScene();
        {HudCoverage coverage(d,true);Check(SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V))),"capture RGB-only game draw");}
        for(auto& v:q)v.c=0x8000ff00;
        {HudCoverage coverage(d,true);d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V));}
        d->EndScene();d->GetRenderTargetData(back,read);D3DLOCKED_RECT lr{};read->LockRect(&lr,nullptr,D3DLOCK_READONLY);
        DWORD c=((DWORD*)lr.pBits)[0];read->UnlockRect();
        Check(((c>>24)&255)>=190&&((c>>24)&255)<=193&&((c>>16)&255)>=63&&((c>>16)&255)<=65&&((c>>8)&255)>=127&&((c>>8)&255)<=129,
            "coverage accumulates alpha without changing native RGB blend");
        DWORD mask=0,separate=1;d->GetRenderState(D3DRS_COLORWRITEENABLE,&mask);d->GetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,&separate);
        Check(mask==7&&!separate,"native color mask and alpha states restored");
        d->SetRenderState(D3DRS_COLORWRITEENABLE,0);
        {HudCoverage coverage(d,true);d->GetRenderState(D3DRS_COLORWRITEENABLE,&mask);Check(mask==0,"stencil-only draws keep alpha untouched");}
        d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
    }
    auto run=[&](hudlayout::Source s,bool move,bool rotate,bool leftAmmo=false,bool full=false,bool text=false,bool nav=false,bool loot=false,unsigned resumes=0){
        auto drawLayout=hudlayout::Partition(layout.health,layout.ammo,full,text);
        IDirect3DTexture9* tex=nullptr;Check(SUCCEEDED(d->CreateTexture(s.textureWidth,s.textureHeight,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&tex,nullptr)),"HUD source");
        D3DLOCKED_RECT map{};tex->LockRect(0,&map,nullptr,0);
        for(unsigned y=0;y<s.textureHeight;y++)for(unsigned x=0;x<s.textureWidth;x++){
            DWORD c=0xffff00ff; // invalid SBS half MUST NEVER appear in either eye
            if(x>=s.x&&y>=s.y&&x<s.x+s.width&&y<s.y+s.height){float u=float(x-s.x)/s.width,v=float(y-s.y)/s.height;
                c=0xff0000ff;if(In(layout.health,u,v))c=0xffff0000;else if(In(layout.ammo,u,v))c=0xff00ff00;
                else if(full&&In(drawLayout.inventory,u,v))c=0xff8080ff;else if(full&&In(drawLayout.resources,u,v))c=0xff00ffff;
                if(u>.44f&&u<.56f&&v>.44f&&v<.56f)c=0x80800000; // premultiplied 50% red over dark background
                if(u>.42f&&u<.58f&&v>.30f&&v<.40f)c=0; // transparent prompt gap
                // A native 12x20px E, with two-pixel strokes, in the prompt.
                // The previous full-width panel reduced it below five rows.
                int gx=int(x-s.x)-int(s.width/2),gy=int(y-s.y)-int(s.height*.35f);
                int raster=std::max(1,int(s.width/1728));
                if(gx>=0&&gx<12*raster&&gy>=0&&gy<20*raster && (gx<2*raster||gy<2*raster||gy>=18*raster||(gy>=9*raster&&gy<11*raster)))c=0xffffffff;
            }
            ((DWORD*)((BYTE*)map.pBits+y*map.Pitch))[x]=c;
        }
        tex->UnlockRect(0);
        Check(SUCCEEDED(d->ColorFill(back,nullptr,0xff202020)),"world fixture");
        D3DMATRIX world={{{2,0,0,0,0,3,0,0,0,0,4,0,100,200,300,1}}};d->SetTransform(D3DTS_WORLD,&world);
        D3DVIEWPORT9 original{7,9,1500,800,0,1};d->SetViewport(&original);
        d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
        xrapi::State pose{};pose.valid=true;pose.hmdOrientation.w=1;pose.hmdPosition={0,1.7f,0};pose.ipd=.064f;
        pose.resumes=resumes;
        pose.left.grip.w=pose.right.grip.w=1;pose.left.position={move?-.12f:-.30f,1.5f,-.7f};pose.right.position={.30f,1.5f,-.7f};
        if(rotate)pose.hmdOrientation={0,sinf(.13f),0,cosf(.13f)};
        gameplayhud::Options options{leftAmmo,loot};
        Check(gameplayhud::Render(d,tex,s,drawLayout,pose,100,2,1.5f,options),"production HUD renders both eyes");
        D3DMATRIX restored{};d->GetTransform(D3DTS_WORLD,&restored);Check(!memcmp(&world,&restored,sizeof(world)),"game world transform restored");
        D3DVIEWPORT9 vp{};d->GetViewport(&vp);Check(!memcmp(&vp,&original,sizeof(vp)),"viewport restored");
        DWORD scissor=0,blend=1;d->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor);d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend);
        Check(scissor&&!blend,"render states restored");
        if(extra){IDirect3DSurface9* actual=nullptr;d->GetRenderTarget(1,&actual);Check(actual==extra,"MRT restored");if(actual)actual->Release();}
        Check(SUCCEEDED(d->GetRenderTargetData(back,read)),"read composed HUD");read->LockRect(&map,nullptr,D3DLOCK_READONLY);Pixels p{};
        unsigned minWhite[2]={864,864},maxWhite[2]={0,0},white[2]={},gold[2]={};
        for(unsigned y=0;y<864;y++)for(unsigned x=0;x<1728;x++){
            DWORD c=((DWORD*)((BYTE*)map.pBits+y*map.Pitch))[x];unsigned e=x/864,r=(c>>16)&255,g=(c>>8)&255,b=c&255;
            if(r>240&&g<10&&b<10){p.red[e]++;p.rx[e]+=x%864;}
            if(g>240&&r<10&&b<10){p.green[e]++;p.gx[e]+=x%864;}
            if(b>240&&r<10&&g<10)p.blue[e]++;
            if(r>240&&b>240&&g<10)p.magenta[e]++;
            if(r>137&&r<150&&g>12&&g<20&&b>12&&b<20)p.half[e]++;
            if(r>160&&g>160&&b>200){white[e]++;p.wx[e]+=x%864;minWhite[e]=std::min(minWhite[e],y);maxWhite[e]=std::max(maxWhite[e],y);}
            if(r>190&&g>140&&g<220&&b>35&&b<140){gold[e]++;p.ax[e]+=x%864;}
            if(g>240&&b>240&&r<10){p.cyan[e]++;p.cx[e]+=x%864;}
            if(r>110&&r<150&&g>110&&g<150&&b>240){p.yellow[e]++;p.yx[e]+=x%864;}
        }
        if(nav){
            FILE* f=fopen("dashboard-stereo.bmp","wb");BITMAPFILEHEADER fh{};BITMAPINFOHEADER ih{};
            ih.biSize=sizeof(ih);ih.biWidth=1728;ih.biHeight=-864;ih.biPlanes=1;ih.biBitCount=32;
            fh.bfType=0x4d42;fh.bfOffBits=sizeof(fh)+sizeof(ih);fh.bfSize=fh.bfOffBits+1728*864*4;
            fwrite(&fh,sizeof(fh),1,f);fwrite(&ih,sizeof(ih),1,f);for(unsigned y=0;y<864;y++)fwrite((BYTE*)map.pBits+y*map.Pitch,4,1728,f);fclose(f);
        }
        read->UnlockRect();tex->Release();
        for(unsigned e=0;e<2;e++){
            Check(p.red[e]>(full?1000:2000)&&p.green[e]>(full?700:1500)&&p.blue[e]>60000,"HP, ammo and uncropped text plane visible per eye");
            Check(p.magenta[e]==0,"inactive texture area excluded");Check(p.half[e]>100,"premultiplied alpha preserved");
            printf("prompt eye %u: %u pixels, rows %u..%u\n",e,white[e],minWhite[e],maxWhite[e]);
            Check(white[e]>=10&&maxWhite[e]>=minWhite[e]+7,"native 20px prompt retains at least eight visible rows per eye");
            Check(gold[e]==0,"HUD contains no duplicate/replacement of the native 3D GPS");
            if(full){Check(p.cyan[e]>200&&p.yellow[e]>200,"native resources and equipment visible on dashboard");p.cx[e]/=p.cyan[e];p.yx[e]/=p.yellow[e];}
            p.white[e]=white[e];p.gold[e]=gold[e];p.wx[e]/=white[e];
            p.rx[e]/=p.red[e];p.gx[e]/=p.green[e];
        }
        printf("HUD %ux%u source %u,%u %ux%u rotated=%d health %.1f/%.1f ammo %.1f/%.1f alpha %u/%u\n",
            s.textureWidth,s.textureHeight,s.x,s.y,s.width,s.height,rotate,p.rx[0],p.rx[1],p.gx[0],p.gx[1],p.half[0],p.half[1]);
        return p;
    };
    auto initial=run(src,false,false),moved=run(src,true,false);
    Check(moved.rx[0]-initial.rx[0]>50&&fabs(moved.gx[0]-initial.gx[0])<1,"independent left/right hand placement");
    Check(initial.rx[0]>initial.rx[1]&&initial.gx[0]>initial.gx[1],"physical positive stereo disparity for both wrist panels");
    auto together=run(src,false,false,true),togetherMoved=run(src,true,false,true);
    Check(togetherMoved.rx[0]-together.rx[0]>50&&togetherMoved.gx[0]-together.gx[0]>50,"health and ammo both follow the left wrist");
    Check(fabs(together.gx[0]-together.rx[0])<10,"ammo aligned with health on the same left panel");
    run(src,false,true);run(offset,false,false);
    gameplayhud::Release();run(hudlayout::Observe(1280,720,0,0,1280,720),false,false);
    run(hudlayout::Observe(1728,864,0,0,1728,864),false,false);
    auto full=run(hudlayout::Observe(3456,1728,0,0,3456,1728),false,false,true,true,true,true);
    auto fullMoved=run(hudlayout::Observe(3456,1728,0,0,3456,1728),true,false,true,true,true,true);
    for(unsigned e=0;e<2;e++){
        Check(fullMoved.rx[e]-full.rx[e]>25&&fullMoved.gx[e]-full.gx[e]>25&&fullMoved.cx[e]-full.cx[e]>25&&fullMoved.yx[e]-full.yx[e]>25,"all four status cards move with the left hand");
        Check(full.white[e]>35,"supersampled enlarged prompt keeps clear strokes");
    }
    auto fixed=run(hudlayout::Observe(3456,1728,0,0,3456,1728),false,false,true,true,true,false,true);
    auto headTurn=run(hudlayout::Observe(3456,1728,0,0,3456,1728),false,true,true,true,true,false,true);
    auto resumed=run(hudlayout::Observe(3456,1728,0,0,3456,1728),false,true,true,true,true,false,true,1);
    Check(fabs(headTurn.wx[0]-fixed.wx[0])>30,"loot text has world-space head-motion parallax");
    Check(fabs(resumed.wx[0]-fixed.wx[0])<10,"tracking resume reanchors loot instead of keeping stale coordinates");
    xrapi::State invalid{};Check(!gameplayhud::Render(d,nullptr,{},layout,invalid,100,2,1.5f),"empty/loading frame rejected");
    gameplayhud::Release();if(extra){d->SetRenderTarget(1,nullptr);extra->Release();}read->Release();back->Release();
    Check(SUCCEEDED(d->Reset(&pp)),"Reset after releasing HUD resources");
    d->Release();api->Release();DestroyWindow(wnd);
    puts("PASS: HUD canvas/UV, lossless partition, two eyes, independent hands, alpha, head turn, state/MRT, resize/Reset, empty frame");
}
