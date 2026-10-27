// Reuse the real D3D9 test helpers, not a second rendering implementation.
#define main ExistingDepthRegression
#include "depth_pipeline.cpp"
#undef main
std::vector<DWORD> Pixels(IDirect3DDevice9* d,unsigned w,unsigned h){
    IDirect3DSurface9 *back=nullptr,*sys=nullptr;d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back);
    HR(d->CreateOffscreenPlainSurface(w,h,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&sys,nullptr),"marker readback surface");
    HR(d->GetRenderTargetData(back,sys),"marker readback");D3DLOCKED_RECT lock{};sys->LockRect(&lock,nullptr,D3DLOCK_READONLY);
    std::vector<DWORD> out(w*h);for(unsigned y=0;y<h;y++)memcpy(out.data()+y*w,(BYTE*)lock.pBits+y*lock.Pitch,w*4);
    sys->UnlockRect();sys->Release();back->Release();return out;
}
float TargetCentre(const std::vector<DWORD>& image,unsigned side,unsigned eye,bool left,float u,float v){
    float sum=0;unsigned count=0;
    for(unsigned y=0;y<side;y++)for(unsigned x=0;x<side;x++){
        if(fabsf(y-v*side)>12||fabsf(x-u*side)>side*.04f+12)continue;
        DWORD p=image[y*side*2+eye*side+x];unsigned r=(p>>16)&255,g=(p>>8)&255,b=p&255;
        bool marker=left?r<140&&g>165&&b>180:r>180&&g>180&&b>180;
        if(marker){sum+=x;count++;}
    }
    Check(count>5,"visible coloured target in each eye");return sum/count;
}
float Scene(IDirect3DDevice9* d,unsigned side,unsigned active,float depth,float fov,const char* image){
    IDirect3DSurface9 *back=nullptr,*ds=nullptr;d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back);d->GetDepthStencilSurface(&ds);
    D3DVIEWPORT9 viewport={0,0,active,active,0,1};d->SetViewport(&viewport);
    stereo::OnClear(d,D3DCLEAR_ZBUFFER,true);d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff204080,1,0);
    d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetTexture(0,nullptr);d->SetTexture(1,nullptr);
    d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);d->SetRenderState(D3DRS_LIGHTING,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,0xf);
    d->BeginScene();Rect(d,0,active,active,depth,0xff204080);d->EndScene();
    stereo::OnClear(d,D3DCLEAR_ZBUFFER,true);d->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,1,0);
    // Simulate a later HUD depth clear/draw; markers must sample the world.
    d->SetRenderState(D3DRS_COLORWRITEENABLE,0);d->BeginScene();Rect(d,0,active,active,.01f,0);d->EndScene();d->SetRenderState(D3DRS_COLORWRITEENABLE,0xf);
    Check(stereo::Compose(d,fov,4.851f,active,active),"marker world composition");
    auto before=Pixels(d,side*2,side);float weapon[]={.65f,.4f},plasmid[]={.25f,.7f};
    Check(stereo::DrawAimReticles(d,weapon,plasmid)==2,"both weapon and plasmid markers drawn");
    CheckTargets(d,back,ds,nullptr,viewport);Check(!theater::g_modDraw,"mod-draw flag restored");
    DWORD z=0;d->GetRenderState(D3DRS_ZENABLE,&z);Check(z==TRUE,"game depth state restored");
    auto after=Pixels(d,side*2,side);SaveBmp(image,after,side*2,side);
    float centre[2]={};
    for(unsigned eye=0;eye<2;eye++){
        centre[eye]=TargetCentre(after,side,eye,false,weapon[0],weapon[1]);
        float left=TargetCentre(after,side,eye,true,plasmid[0],plasmid[1]);
        float raw=(4.851f*.5f)/(2*tanf(fov*vr::kPi/360))*(.1f-.09998474f*depth);
        float span=.025f*.6f,knee=.025f-span,excess=fmaxf(raw-knee,0);
        float shift=fminf(.025f,fminf(raw,knee)+span*excess/(span+excess));
        float delta=(eye?-1.f:1.f)*shift*side;
        Check(fabsf(centre[eye]-(weapon[0]*side+delta))<1.2f,"weapon marker uses world surface disparity");
        Check(fabsf(left-(plasmid[0]*side+delta))<1.2f,"plasmid marker uses world surface disparity");
    }
    unsigned changed=0;for(unsigned i=0;i<before.size();i++)changed+=(before[i]&0xffffff)!=(after[i]&0xffffff);
    Check(changed<1200,"small local draws preserve the rest of the world");
    g_config.aimReticles=false;Check(stereo::DrawAimReticles(d,weapon,plasmid)==0,"setting disables targets");g_config.aimReticles=true;
    float bad[]={NAN,.5f},outside[]={1.2f,.4f};Check(stereo::DrawAimReticles(d,bad,outside)==0,"invalid/offscreen targets rejected");
    stereo::EndFrame();Check(stereo::DrawAimReticles(d,weapon,plasmid)==0,"stale world depth rejected after frame/menu transition");
    back->Release();ds->Release();printf("targets: z=%.3f active=%u fov=%.0f centres=%.2f/%.2f changed=%u\n",depth,active,fov,centre[0],centre[1],changed);
    return centre[0]-centre[1];
}
void Occlusion(IDirect3DDevice9* d,unsigned side){
    D3DVIEWPORT9 viewport={0,0,side,side,0,1};d->SetViewport(&viewport);
    stereo::OnClear(d,D3DCLEAR_ZBUFFER,true);d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff204080,1,0);
    d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetTexture(0,nullptr);d->SetTexture(1,nullptr);
    d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);d->SetRenderState(D3DRS_LIGHTING,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,0xf);
    d->BeginScene();Rect(d,0,side,side,.98f,0xff204080);Rect(d,0,side*.49f,side,.5f,0xffa02020);d->EndScene();
    stereo::OnClear(d,D3DCLEAR_ZBUFFER,true);d->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,1,0);
    Check(stereo::Compose(d,100,4.851f,side,side),"occlusion world");auto before=Pixels(d,side*2,side);
    float target[]={.5f,.5f};Check(stereo::DrawAimReticles(d,target,nullptr)==1,"occluded target pass");
    auto after=Pixels(d,side*2,side);unsigned row=side/2;
    Check((before[row*side*2+side/2]&0xffffff)==(after[row*side*2+side/2]&0xffffff),"near silhouette hides far marker in the left eye");
    unsigned p=after[row*side*2+side+side/2-1];
    Check(((p>>16)&255)>180&&((p>>8)&255)>180&&(p&255)>180,"unoccluded right-eye target stays visible");
    SaveBmp("aim-occlusion.bmp",after,side*2,side);stereo::EndFrame();
    puts("PASS: actual nearer silhouette occludes the far target independently in each eye");
}
int main(){
    g_config.enabled=g_config.depthStereo=g_config.weaponGeometry=g_config.aimReticles=true;
    g_config.stereo=false;g_config.depthMaxShift=.025f;g_config.depthNearSoftness=.6f;g_config.gameWidth=g_config.gameHeight=256;
    WNDCLASSA wc{};wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(nullptr);wc.lpszClassName="AimRegression";RegisterClassA(&wc);
    HWND window=CreateWindowA(wc.lpszClassName,"Hidden aim regression",WS_POPUP,0,0,512,256,nullptr,nullptr,wc.hInstance,nullptr);
    IDirect3D9* api=Direct3DCreate9(D3D_SDK_VERSION);Check(api!=nullptr,"D3D9");D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth=512;pp.BackBufferHeight=256;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24S8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    stereo::AdjustParams(&pp);IDirect3DDevice9* d=nullptr;HR(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d),"aim test device");
    stereo::AfterCreate(d,&pp);Check(stereo::Ready(),"world depth ready");
    float nearDisparity=Scene(d,256,256,.5f,100,"aim-near.bmp"),farDisparity=Scene(d,256,256,.98f,100,"aim-far.bmp");
    Check(nearDisparity>farDisparity+4,"surface depth changes binocular target distance");
    Scene(d,256,192,.7f,110,"aim-dynamic.bmp");
    Occlusion(d,256);
    d->SetDepthStencilSurface(nullptr);stereo::Release();pp.EnableAutoDepthStencil=TRUE;stereo::AdjustParams(&pp);
    HR(d->Reset(&pp),"reset releases reticle resources");stereo::AfterCreate(d,&pp);Scene(d,256,256,.7f,100,"aim-reset.bmp");
    d->SetDepthStencilSurface(nullptr);stereo::Release();d->Release();api->Release();DestroyWindow(window);
    puts("PASS: actual GPU weapon/plasmid targets, surface stereo depth, foreground exclusion, dynamic UVs, bounded draws, state restoration and reset");
}
