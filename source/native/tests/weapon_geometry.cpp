// Hidden GPU regression for the production weapon layer. No game or headset.
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <vector>
#include <initializer_list>
#include <utility>
#include "../src/config.h"
#include "../src/vrframe.h"
#include "../src/vertexstereo.h"
#include "../src/weaponlayer.h"
#include "../src/foregroundroute.h"
#include "../src/handmask.h"
#include "../src/samplermask.h"
Config g_config;
namespace theater {volatile bool g_modDraw=false;}
namespace vrframe {Frame Current(){Frame f{};f.active=f.inGame=true;f.fov=100;f.ipdUnits=4.851f;return f;}}
void Log(const char* format,...){va_list a;va_start(a,format);vprintf(format,a);va_end(a);puts("");}
void check(bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
void hr(HRESULT result,const char* why){if(FAILED(result)){fprintf(stderr,"FAIL: %s hr=%08lx\n",why,result);exit(1);}}
std::vector<DWORD> load(const char* name){FILE* f=fopen(name,"rb");check(f!=nullptr,"open shader fixture");fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<DWORD> b(n/4);fread(b.data(),1,n,f);fclose(f);return b;}
HRESULT STDMETHODCALLTYPE draw(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,INT b,UINT m,UINT v,UINT s,UINT c){return d->DrawIndexedPrimitive(t,b,m,v,s,c);}
HRESULT STDMETHODCALLTYPE drawOrdinary(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT s,UINT c){return d->DrawPrimitive(t,s,c);}
HRESULT STDMETHODCALLTYPE drawUP(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT c,const void* v,UINT s){return d->DrawPrimitiveUP(t,c,v,s);}
HRESULT STDMETHODCALLTYPE noDraw(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT){return D3D_OK;}
struct Vertex {float x,y,z;DWORD color;};
void HdrChain(IDirect3DDevice9* d,IDirect3DSurface9* back,IDirect3DSurface9* originalDepth,IDirect3DSurface9* sys,
              IDirect3DVertexBuffer9* vb,IDirect3DIndexBuffer9* ib,IDirect3DVertexDeclaration9* decl){
 weaponlayer::Release();d->SetRenderTarget(1,nullptr);
 IDirect3DTexture9 *hdr=nullptr,*middle=nullptr,*exposure=nullptr;IDirect3DSurface9 *hdrRT=nullptr,*midRT=nullptr;
 hr(d->CreateTexture(1024,512,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&hdr,nullptr),"native HDR input");hdr->GetSurfaceLevel(0,&hdrRT);
 hr(d->CreateTexture(512,256,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&middle,nullptr),"downsampled HDR intermediate");middle->GetSurfaceLevel(0,&midRT);
 hr(d->CreateTexture(1,1,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&exposure,nullptr),"shared world exposure");
 D3DLOCKED_RECT ex{};exposure->LockRect(0,&ex,nullptr,0);*(float*)ex.pBits=.5f;exposure->UnlockRect(0);
 auto vsCode=load("weapon-vs3.cso"),hdrCode=load("weapon-hdr-ps.cso"),halfCode=load("post-half.cso"),toneCode=load("post-tone.cso");
 IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9 *material=nullptr,*half=nullptr,*tone=nullptr;
 hr(d->CreateVertexShader(vsCode.data(),&vs),"HDR original VS");hr(d->CreatePixelShader(hdrCode.data(),&material),"HDR original material");
 hr(d->CreatePixelShader(halfCode.data(),&half),"native intermediate shader");hr(d->CreatePixelShader(toneCode.data(),&tone),"native exposure/tone shader");
 Vertex* vertices=nullptr;vb->Lock(0,0,(void**)&vertices,0);
 vertices[0]={-1,-1,20,0x00ff0000};vertices[1]={-1,1,20,0x00ff0000};vertices[2]={1,-1,20,0x00ff0000};vertices[3]={1,1,20,0x00ff0000};vb->Unlock();
 d->SetTexture(0,nullptr);d->SetTexture(1,nullptr);d->SetTexture(2,nullptr);d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,hdrRT);d->SetDepthStencilSurface(originalDepth);
 D3DVIEWPORT9 viewport={0,0,512,512,0,1};d->SetViewport(&viewport);d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0);
 d->SetStreamSource(0,vb,0,sizeof(Vertex));d->SetIndices(ib);d->SetVertexDeclaration(decl);d->SetVertexShader(vs);d->SetPixelShader(material);
 float focal=1/tanf(100*vr::kPi/360),f=65536,n=10;
 float matrix[16]={focal,0,0,0,0,focal,0,0,0,0,f/(f-n),-n*f/(f-n),0,0,1,0};d->SetVertexShaderConstantF(0,matrix,4);
 d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_ZENABLE,TRUE);
 d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);d->SetRenderState(D3DRS_ZFUNC,D3DCMP_ALWAYS);d->SetRenderState(D3DRS_COLORWRITEENABLE,0xf);
 hr(d->BeginScene(),"HDR geometry begin");
 check(weaponlayer::DrawIndexed(d,D3DPT_TRIANGLELIST,0,0,4,0,2,draw),"first HDR capture owns source immediately, no temporary mono weapon");
 hr(d->EndScene(),"HDR geometry end");
 check(!weaponlayer::SourceRemovalReady(),"HDR pair is not marked displayed before its tone chain");
 auto post=[&](IDirect3DSurface9* target,IDirect3DTexture9* input,IDirect3DPixelShader9* shader,unsigned side){
  d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,target);D3DVIEWPORT9 vp={0,0,side,side,0,1};d->SetViewport(&vp);
  d->SetVertexShader(nullptr);d->SetPixelShader(shader);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetTexture(0,input);d->SetTexture(1,exposure);
  d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
  for(unsigned s=0;s<2;s++){d->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE);}
  struct Q {float x,y,z,w,u,v;};float end=side-.5f;
  Q q[4]={{-.5f,-.5f,0,1,0,0},{end,-.5f,0,1,.5f,0},{-.5f,end,0,1,0,1},{end,end,0,1,.5f,1}};
  hr(d->BeginScene(),"native postprocess begin");hr(drawUP(d,D3DPT_TRIANGLESTRIP,2,q,sizeof(Q)),"original postprocess");
  weaponlayer::ReplayPostProcess(d,D3DPT_TRIANGLESTRIP,2,q,sizeof(Q),drawUP);hr(d->EndScene(),"native postprocess end");
  IDirect3DSurface9* actual=nullptr;d->GetRenderTarget(0,&actual);check(actual==target,"postprocess target restored");actual->Release();
  IDirect3DBaseTexture9* actualInput=nullptr;d->GetTexture(0,&actualInput);check(actualInput==input,"original HDR sampler restored");actualInput->Release();
 };
 post(midRT,hdr,half,256);post(back,middle,tone,512);
 auto work=weaponlayer::TakeReplayWork();
 check(work.calls==2&&work.textureReads==3&&work.replayedDraws==4,"native two-stage chain reads 3 texture slots instead of 32; both eye passes retained");
 // A constant/partial draw can leave a tracked input bound in an unused slot.
 // Preserve the legacy replay decision without modifying pixels in this probe.
 d->SetPixelShader(material);float probe[24]={};
 weaponlayer::ReplayPostProcess(d,D3DPT_TRIANGLESTRIP,2,probe,24,noDraw);
 work=weaponlayer::TakeReplayWork();
 check(work.calls==1&&work.textureReads==1&&work.replayedDraws==2,"unused tracked binding retains conservative native-chain replay");
 d->ColorFill(back,nullptr,0xff204080);weaponlayer::Composite(d);
 check(weaponlayer::SourceRemovalReady(),"source removal armed only after native HDR chain and displayed model");
 hr(d->GetRenderTargetData(back,sys),"HDR final readback");D3DLOCKED_RECT lock{};sys->LockRect(&lock,nullptr,D3DLOCK_READONLY);
 FILE* snapshot=fopen("hdr-final.bgra","wb");check(snapshot!=nullptr,"HDR pixel comparison artifact");
 for(unsigned y=0;y<512;y++)fwrite((BYTE*)lock.pBits+y*lock.Pitch,4,1024,snapshot);fclose(snapshot);
 const DWORD* row=(const DWORD*)((BYTE*)lock.pBits+256*lock.Pitch);double centers[2]={};unsigned counts[2]={};
 for(unsigned eye=0;eye<2;eye++)for(unsigned x=0;x<512;x++){
  DWORD c=row[eye*512+x];unsigned red=(c>>16)&255;
  if(red>=125&&red<=130&&((c>>8)&255)<3&&(c&255)<3){counts[eye]++;centers[eye]+=x;}
 }
 for(unsigned eye=0;eye<2;eye++){check(counts[eye]>=18&&counts[eye]<=24,"HDR material uses original two-stage tone mapping and shared world exposure");centers[eye]/=counts[eye];}
 check(fabs(centers[0]-centers[1]-4.851*focal/20*256)<3,"HDR stages preserve physical per-eye geometry and native UVs");
 sys->UnlockRect();weaponlayer::EndFrame();
 weaponlayer::InvalidateModel();check(!weaponlayer::SourceRemovalReady(),"model switch disarms previous HDR source suppression");
 d->ColorFill(back,nullptr,0xff204080);weaponlayer::Composite(d);hr(d->GetRenderTargetData(back,sys),"model handoff readback");
 sys->LockRect(&lock,nullptr,D3DLOCK_READONLY);row=(const DWORD*)((BYTE*)lock.pBits+256*lock.Pitch);
 for(unsigned x=0;x<1024;x++)check(row[x]==0xff204080,"previous weapon cannot leak into newly selected model");
 sys->UnlockRect();
 // Repeated selections reuse existing HDR resources. None of their first
 // captures may write a temporary mono weapon into the world's source.
 IDirect3DSurface9* hdrRead=nullptr;
 hr(d->CreateOffscreenPlainSurface(1024,512,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&hdrRead,nullptr),"HDR source readback");
 for(float distance:{10.f,20.f,40.f}){
  weaponlayer::InvalidateModel();
  vb->Lock(0,0,(void**)&vertices,0);for(unsigned i=0;i<4;i++)vertices[i].z=distance;vb->Unlock();
  d->SetTexture(0,nullptr);d->SetTexture(1,nullptr);d->SetTexture(2,nullptr);
  d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,hdrRT);d->SetDepthStencilSurface(originalDepth);d->SetViewport(&viewport);
  d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0);
  d->SetStreamSource(0,vb,0,sizeof(Vertex));d->SetIndices(ib);d->SetVertexDeclaration(decl);d->SetVertexShader(vs);d->SetPixelShader(material);
  d->SetVertexShaderConstantF(0,matrix,4);d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);
  d->SetRenderState(D3DRS_ZFUNC,D3DCMP_ALWAYS);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
  hr(d->BeginScene(),"selected HDR model begin");
  check(weaponlayer::DrawIndexed(d,D3DPT_TRIANGLELIST,0,0,4,0,2,draw),"new HDR selection is consumed on its first draw");
  hr(d->EndScene(),"selected HDR model end");
  hr(d->GetRenderTargetData(hdrRT,hdrRead),"native HDR source after capture");
  hdrRead->LockRect(&lock,nullptr,D3DLOCK_READONLY);
  for(unsigned y=0;y<512;y++)for(unsigned x=0;x<1024*8;x++)check(((BYTE*)lock.pBits)[y*lock.Pitch+x]==0,"no temporary mono model leaks into any world-source pixel");
  hdrRead->UnlockRect();
  d->ColorFill(back,nullptr,0xff204080);weaponlayer::Composite(d);
  hr(d->GetRenderTargetData(back,sys),"incomplete HDR pair is not published");sys->LockRect(&lock,nullptr,D3DLOCK_READONLY);
  row=(const DWORD*)((BYTE*)lock.pBits+256*lock.Pitch);for(unsigned x=0;x<1024;x++)check(row[x]==0xff204080,"no stale/partial model during tone warmup");sys->UnlockRect();
  post(midRT,hdr,half,256);post(back,middle,tone,512);
  d->ColorFill(back,nullptr,0xff204080);weaponlayer::Composite(d);
  hr(d->GetRenderTargetData(back,sys),"new stereo selection published");sys->LockRect(&lock,nullptr,D3DLOCK_READONLY);
  row=(const DWORD*)((BYTE*)lock.pBits+256*lock.Pitch);double centre[2]={};unsigned pixels[2]={};
  for(unsigned eye=0;eye<2;eye++)for(unsigned x=0;x<512;x++)if(((row[eye*512+x]>>16)&255)>=125&&((row[eye*512+x]>>8)&255)<3){centre[eye]+=x;pixels[eye]++;}
  for(unsigned eye=0;eye<2;eye++){check(pixels[eye]>4,"new model visible in both eyes on the same frame as its completed HDR chain");centre[eye]/=pixels[eye];}
  check(fabs(centre[0]-centre[1]-4.851*focal/distance*256)<4,"selected HDR model has current depth/stereo, never the previous model's plane");
  sys->UnlockRect();weaponlayer::EndFrame();
 }
 hdrRead->Release();
 d->SetTexture(0,nullptr);d->SetTexture(1,nullptr);d->SetTexture(2,nullptr);d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);
 hdrRT->Release();midRT->Release();hdr->Release();middle->Release();exposure->Release();vs->Release();material->Release();half->Release();tone->Release();
 puts("PASS: HDR geometry, downsampled intermediate, native nonlinear tone shaders, shared world exposure, source-removal bootstrap, shader/target/UV restore");
 puts("PASS: HDR chain texture reads 32 -> 3, four per-eye draws unchanged; constant/partial chain fallback retained");
 puts("PASS: three HDR model handoffs, zero mono-source pixels, no stale/partial warmup, same-frame completed stereo at current depth");
}
void PlasmidSubset(IDirect3DDevice9* d,IDirect3DSurface9* back,IDirect3DSurface9* originalDepth,IDirect3DSurface9* sys,IDirect3DVertexDeclaration9* decl,IDirect3DPixelShader9* ps){
 weaponlayer::Release();d->SetRenderTarget(1,nullptr);
 FILE* f=fopen("hand-indices.bin","rb");check(f!=nullptr,"real native hand topology fixture");
 std::vector<unsigned short> source(handmaskdata::triangles*3);check(fread(source.data(),2,source.size(),f)==source.size(),"read all hand indices");fclose(f);
 f=fopen("hand-flags.bin","rb");check(f!=nullptr,"skin-weight reference flags");std::vector<unsigned char> flags(2180);check(fread(flags.data(),1,flags.size(),f)==flags.size(),"read hand skin reference");fclose(f);
 IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
 hr(d->CreateVertexBuffer(2180*sizeof(Vertex),0,0,D3DPOOL_MANAGED,&vb,nullptr),"plasmid source vertices");
 hr(d->CreateIndexBuffer(unsigned(source.size()*2),0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib,nullptr),"plasmid source indices");
 void* data=nullptr;ib->Lock(0,0,&data,0);memcpy(data,source.data(),source.size()*2);ib->Unlock();
 Vertex* v=nullptr;vb->Lock(0,0,(void**)&v,0);
 for(unsigned i=0;i<2180;i++)v[i]={float(i%20)/10-1,float(i/20%20)/10-1,30.f+(i%7)*.01f,flags[i]?0x0000ff00u:0x00ff0000u};vb->Unlock();
 auto code=load("weapon-vs3.cso");IDirect3DVertexShader9* vs=nullptr;hr(d->CreateVertexShader(code.data(),&vs),"hand shader");
 d->SetDepthStencilSurface(originalDepth);d->SetRenderTarget(0,back);D3DVIEWPORT9 vp={0,0,512,512,0,1};d->SetViewport(&vp);
 d->SetStreamSource(0,vb,0,sizeof(Vertex));d->SetIndices(ib);d->SetVertexDeclaration(decl);d->SetVertexShader(vs);d->SetPixelShader(ps);
 const float focal=1/tanf(100*vr::kPi/360),farClip=65536,nearClip=10;
 float matrix[16]={focal,0,0,0,0,focal,0,0,0,0,farClip/(farClip-nearClip),-nearClip*farClip/(farClip-nearClip),0,0,1,0};d->SetVertexShaderConstantF(0,matrix,4);
 d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_ZENABLE,TRUE);
 d->SetRenderState(D3DRS_ZFUNC,D3DCMP_ALWAYS);d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,0);
 hr(d->BeginScene(),"mask-only native pass");check(!weaponlayer::DrawIndexed(d,D3DPT_TRIANGLELIST,0,0,2180,0,3588,draw),"colorless depth/mask pass cannot create black coverage");d->EndScene();
 d->SetRenderState(D3DRS_COLORWRITEENABLE,0xf);d->ColorFill(back,nullptr,0xff204080);
 hr(d->BeginScene(),"plasmid native hand");check(weaponlayer::DrawPlasmidHand(d,D3DPT_TRIANGLELIST,0,0,2180,0,3588,draw),"verified hand captured in stereo");d->EndScene();
 IDirect3DIndexBuffer9* actual=nullptr;d->GetIndices(&actual);check(actual==ib,"native index binding restored");actual->Release();
 weaponlayer::Composite(d);hr(d->GetRenderTargetData(back,sys),"plasmid stereo readback");D3DLOCKED_RECT lock{};sys->LockRect(&lock,nullptr,D3DLOCK_READONLY);
 unsigned green[2]={};
 for(unsigned y=0;y<512;y++){auto row=(const DWORD*)((const BYTE*)lock.pBits+y*lock.Pitch);for(unsigned x=0;x<1024;x++){
  unsigned red=(row[x]>>16)&255,g=(row[x]>>8)&255,b=row[x]&255;
  check(!(red>200&&g<30&&b<30),"right hand and arm triangles never appear");if(g>200&&red<30&&b<30)green[x/512]++;
 }}sys->UnlockRect();check(green[0]>100&&green[1]>100,"isolated palm visible in both eyes");
 weaponlayer::EndFrame();weaponlayer::Release();d->SetVertexShader(nullptr);d->SetStreamSource(0,nullptr,0,0);d->SetIndices(nullptr);vs->Release();vb->Release();ib->Release();
 puts("PASS: native 3588-triangle hand -> 1228-triangle palm in both eyes, arms absent, colorless masks rejected, original index binding restored");
}
int main(int argc,char** argv){
 for(const auto& item:std::vector<std::pair<const char*,unsigned>>{{"post-half.cso",1},{"post-tone.cso",3},{"weapon-ps.cso",0}}){
  auto code=load(item.first);check(samplermask::Read(code.data(),code.size())==item.second,"compiled shader declares only its real samplers");
 }
 {
  const DWORD sampler=vertexstereo::Register(D3DSPR_SAMPLER,15);
  DWORD sparse[]={D3DPS_VERSION(2,0),(2u<<24)|D3DSIO_DCL,D3DSTT_2D,sampler,D3DSIO_END};
  check(samplermask::Read(sparse,5)==0x8000,"PS2 high sampler slot retained");
  check(samplermask::Read(sparse,4)==samplermask::all,"truncated program conservatively checks every slot");
  DWORD immediate[]={D3DPS_VERSION(3,0),(5u<<24)|D3DSIO_DEF,vertexstereo::Register(D3DSPR_CONST,0),sampler,0,0,0,
      (2u<<16)|D3DSIO_COMMENT,sampler,sampler,D3DSIO_END};
  check(samplermask::Read(immediate,11)==0,"literal constants and comments cannot declare samplers");
  immediate[0]=D3DPS_VERSION(1,4);check(samplermask::Read(immediate,11)==samplermask::all,"PS1 conservative fallback");
  check(samplermask::Read(nullptr,0)==samplermask::all,"fixed-function conservative fallback");
 }
 puts("PASS: compiled PS2/3 sampler masks, high slots, constants/comments and bounded conservative fallbacks");
 if(argc>2){
  FILE* list=fopen(argv[2],"rb");check(list!=nullptr,"native pixel pack");unsigned total=0,count=0;
  check(fread(&total,4,1,list)==1&&total>0&&total<100000,"bounded pixel pack count");
  for(;count<total;count++){
   unsigned mask=0,bytes=0;check(fread(&mask,4,1,list)==1&&fread(&bytes,4,1,list)==1&&bytes>=8&&bytes<=65536&&!(bytes%4),"native pixel record");
   std::vector<DWORD> code(bytes/4);check(fread(code.data(),1,bytes,list)==bytes,"complete native pixel bytecode");
   check(samplermask::Read(code.data(),code.size())==mask,"native pixel sampler mask agrees with D3DDisassemble");
  }
  fclose(list);check(count>0,"native pixel coverage");printf("PASS: %u real BioShock pixel sampler masks agree with Microsoft D3DDisassemble\n",count);
 }
 for(uintptr_t site : {0x10b917b4u,0x10b91818u,0x10b91888u,0x10b918d3u,0x10b917cdu,0x10b91838u}){
  using A=foregroundroute::Action;
  check(foregroundroute::Decide(site,true,true)==A::Capture,"selected mesh captured without any optional canvas command");
  check(foregroundroute::Decide(site,true,false)==A::Keep,"world meshes are never redirected");
  check(foregroundroute::Decide(site,false,true)==A::Keep,"menu/loading preserves selected mesh");
 }
 for(uintptr_t site : {0x10b91e43u,0x10b8a300u,0u})check(foregroundroute::Decide(site,true,true)==foregroundroute::Action::Keep,"canvas and Flash excluded from weapon layer");
 check(foregroundroute::WrenchMesh(true,1953,1686),"actual Quest wrench signature");
 check(!foregroundroute::WrenchMesh(false,1953,1686)&&!foregroundroute::WrenchMesh(true,1953,100)&&!foregroundroute::WrenchMesh(true,2180,3588),"other actor/submesh and Jack arms rejected");
 for(auto pair : {std::pair<unsigned,unsigned>{2361,2824},{1687,1803},{889,916},{312,269}}){
  check(foregroundroute::PistolMesh(true,pair.first,pair.second),"verified native pistol LOD routed");
  check(!foregroundroute::PistolMesh(false,pair.first,pair.second)&&!foregroundroute::PistolMesh(true,pair.first,pair.second+1),"pistol requires the selected named model and full mesh signature");
 }
 for(auto pair : {std::pair<unsigned,unsigned>{2180,3588},{1953,1686},{36,30},{234,360},{300,264},{67,78}})
  check(!foregroundroute::PistolMesh(true,pair.first,pair.second),"pistol route excludes hands, wrench, light volumes and particles from the actual trace");
 check(foregroundroute::NavigationMesh(true,67,78),"original GPS arrow shares the weapon geometry route");
 check(!foregroundroute::NavigationMesh(false,67,78)&&!foregroundroute::NavigationMesh(true,67,79)&&!foregroundroute::NavigationMesh(true,1953,1686),"GPS requires verified visible native actor and exact mesh");
 g_config.enabled=g_config.depthStereo=g_config.weaponGeometry=true;g_config.weaponNearScale=.1f;
 HWND w=CreateWindowExW(0,L"STATIC",L"Hidden weapon regression",WS_OVERLAPPED,0,0,1024,512,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 IDirect3D9* api=Direct3DCreate9(D3D_SDK_VERSION);check(api!=nullptr,"D3D9");
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=w;pp.BackBufferWidth=1024;pp.BackBufferHeight=512;
 pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24S8;
 IDirect3DDevice9* d=nullptr;hr(api->CreateDevice(0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d),"hidden GPU device");
 IDirect3DVertexDeclaration9* decl=nullptr;
 const D3DVERTEXELEMENT9 elems[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
 hr(d->CreateVertexDeclaration(elems,&decl),"geometry declaration");
 IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
 hr(d->CreateVertexBuffer(4*sizeof(Vertex),0,0,D3DPOOL_DEFAULT,&vb,nullptr),"weapon vertex buffer");
 hr(d->CreateIndexBuffer(6*sizeof(WORD),0,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib,nullptr),"weapon indices");
 WORD* indices=nullptr;ib->Lock(0,0,(void**)&indices,0);WORD triangles[]={0,1,2,2,1,3};memcpy(indices,triangles,sizeof(triangles));ib->Unlock();
 IDirect3DSurface9 *back=nullptr,*originalDepth=nullptr,*sys=nullptr,*mrt=nullptr;
 hr(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back),"main backbuffer");hr(d->GetDepthStencilSurface(&originalDepth),"main depth");
 hr(d->CreateOffscreenPlainSurface(1024,512,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&sys,nullptr),"readback");
 D3DCAPS9 caps{};d->GetDeviceCaps(&caps);
 if(caps.NumSimultaneousRTs>1){hr(d->CreateRenderTarget(1024,512,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&mrt,nullptr),"game MRT");d->SetRenderTarget(1,mrt);d->SetRenderState(D3DRS_COLORWRITEENABLE1,0);}
 auto psCode=load("weapon-ps.cso");IDirect3DPixelShader9* ps=nullptr;hr(d->CreatePixelShader(psCode.data(),&ps),"material with zero alpha");
 IDirect3DTexture9* worldZ=nullptr;hr(d->CreateTexture(1024,512,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&worldZ,nullptr),"per-eye world depth fixture");
 struct Mode {const char* name;const char* file;bool ordinary;bool fixedPixel;};
 for(const Mode mode : {Mode{"VS2 indexed","weapon-vs2.cso",false,false},
       {"VS3 indexed","weapon-vs3.cso",false,false},{"VS3 branch","weapon-vs3-branch.cso",false,false},
       {"VS3 skeletal bones","weapon-vs3-skin.cso",false,false},
       {"VS3 ordinary strip","weapon-vs3.cso",true,false},{"fixed indexed",nullptr,false,true},
       {"fixed vertex + material PS ordinary",nullptr,true,false}}){
  const bool fixed=!mode.file;vertexstereo::Program patched;IDirect3DVertexShader9* vs=nullptr;
  if(!fixed){
   auto code=load(mode.file);patched=vertexstereo::Patch(code.data(),code.size());check(!patched.error,"supported shader extension");
   {FILE* output=fopen("patched-last.cso","wb");fwrite(patched.code.data(),4,patched.code.size(),output);fclose(output);}
   auto truncated=vertexstereo::Patch(code.data(),code.size()-1);check(truncated.error!=nullptr,"reject missing END");
   hr(d->CreateVertexShader(code.data(),&vs),"original vertex shader");
  }
  printf("Testing %s\n",mode.name);
  for(float relocation : {1.f,2.f})for(float distance : {5.f,10.f,20.f,40.f}){
   Vertex* v=nullptr;vb->Lock(0,0,(void**)&v,0);
   const float x=fixed?-2.f:0,z=distance+(fixed?13.f:0);
   v[0]={x-1,-1,z,0x00ff0000};v[1]={x-1,1,z,0x00ff0000};v[2]={x+1,-1,z,0x00ff0000};v[3]={x+1,1,z,0x00ff0000};vb->Unlock();
   d->SetStreamSource(0,vb,0,sizeof(Vertex));d->SetIndices(ib);d->SetVertexDeclaration(decl);d->SetVertexShader(vs);d->SetPixelShader(mode.fixedPixel?nullptr:ps);
   const float focal=1/tanf(100*vr::kPi/360),farClip=65536,nearClip=10;
   float matrix[16]={focal,0,0,0,0,focal,0,0,0,0,farClip/(farClip-nearClip),-nearClip*farClip/(farClip-nearClip),0,0,1,0};
   D3DMATRIX identity{},view{},projection{};identity._11=identity._22=identity._33=identity._44=1;view=identity;
   if(fixed){view._41=2;view._43=-13;}
   projection._11=projection._22=focal;projection._33=farClip/(farClip-nearClip);projection._43=-nearClip*farClip/(farClip-nearClip);projection._34=1;
   d->SetTransform(D3DTS_WORLD,&identity);d->SetTransform(D3DTS_VIEW,&view);d->SetTransform(D3DTS_PROJECTION,&projection);
   if(!fixed)d->SetVertexShaderConstantF(0,matrix,4);
   const float sentinel[4]={123,45,6,7};
   if(!fixed){
    if(patched.textureParams){float bones[24*4]={};d->SetVertexShaderConstantF(4,bones,24);}
    else d->SetVertexShaderConstantF(patched.constant,sentinel,1);
   }
   float savedBank[256*4]={};d->GetVertexShaderConstantF(0,savedBank,256);
   IDirect3DTexture9* priorVertexTexture=nullptr;
   if(!fixed&&patched.textureParams){
    hr(d->CreateTexture(1,1,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&priorVertexTexture,nullptr),"original game vertex sampler");
    hr(d->SetTexture(D3DVERTEXTEXTURESAMPLER0+patched.sampler,priorVertexTexture),"original vertex texture bind");
    d->SetSamplerState(D3DVERTEXTEXTURESAMPLER0+patched.sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_MIRROR);
   }
   d->SetRenderState(D3DRS_LIGHTING,FALSE);d->SetRenderState(D3DRS_FOGENABLE,FALSE);
   d->SetTexture(0,nullptr);d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
   d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
   d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
   D3DVIEWPORT9 vp={0,0,512,512,0,1};d->SetViewport(&vp);
   d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
   d->SetRenderState(D3DRS_ZFUNC,D3DCMP_ALWAYS);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_STENCILENABLE,FALSE);
   hr(d->ColorFill(back,nullptr,0xff204080),"full unobscured background");
   hr(d->BeginScene(),"game foreground scene");
   check(mode.ordinary?weaponlayer::DrawPrimitive(d,D3DPT_TRIANGLESTRIP,0,2,drawOrdinary,relocation):
         weaponlayer::DrawIndexed(d,D3DPT_TRIANGLELIST,0,0,4,0,2,draw,relocation),"weapon redirected into actual geometry stereo");
   hr(d->EndScene(),"game foreground complete");
   IDirect3DSurface9* restored=nullptr;d->GetRenderTarget(0,&restored);check(restored==back,"game target restored");restored->Release();
   d->GetDepthStencilSurface(&restored);check(restored==originalDepth,"game depth restored");restored->Release();
   if(mrt){d->GetRenderTarget(1,&restored);check(restored==mrt,"game MRT restored");restored->Release();}
   D3DVIEWPORT9 actual{};d->GetViewport(&actual);check(actual.Width==512&&actual.X==0,"game viewport restored");
   if(!fixed){float bank[256*4];d->GetVertexShaderConstantF(0,bank,256);check(!memcmp(savedBank,bank,sizeof(bank)),"entire material and skeletal bone constant bank retained");}
   if(priorVertexTexture){
    IDirect3DBaseTexture9* actualTexture=nullptr;d->GetTexture(D3DVERTEXTEXTURESAMPLER0+patched.sampler,&actualTexture);
    check(actualTexture==priorVertexTexture,"original vertex texture restored");actualTexture->Release();
    DWORD address=0;d->GetSamplerState(D3DVERTEXTEXTURESAMPLER0+patched.sampler,D3DSAMP_ADDRESSU,&address);
    check(address==D3DTADDRESS_MIRROR,"original vertex sampler state restored");
    d->SetTexture(D3DVERTEXTEXTURESAMPLER0+patched.sampler,nullptr);priorVertexTexture->Release();
   }
   D3DMATRIX actualMatrix{};d->GetTransform(D3DTS_VIEW,&actualMatrix);check(!memcmp(&view,&actualMatrix,sizeof(view)),"translated game view restored");
   d->GetTransform(D3DTS_PROJECTION,&actualMatrix);check(!memcmp(&projection,&actualMatrix,sizeof(projection)),"game projection restored");
   IDirect3DVertexShader9* actualVS=nullptr;d->GetVertexShader(&actualVS);check(actualVS==vs,"original vertex path restored");if(actualVS)actualVS->Release();
   DWORD state=0;d->GetRenderState(D3DRS_ZWRITEENABLE,&state);check(state==FALSE,"game depth write flag restored");
   hr(d->GetRenderTargetData(back,sys),"world before weapon composite");D3DLOCKED_RECT lock{};sys->LockRect(&lock,nullptr,D3DLOCK_READONLY);
   check(((DWORD*)((BYTE*)lock.pBits+256*lock.Pitch))[256]==0xff204080,"weapon never covers centre-camera background");sys->UnlockRect();
   weaponlayer::Composite(d);
   hr(d->GetRenderTargetData(back,sys),"composite readback");sys->LockRect(&lock,nullptr,D3DLOCK_READONLY);
   const DWORD* row=(const DWORD*)((BYTE*)lock.pBits+256*lock.Pitch);double centre[2]={};unsigned widths[2]={};
   for(unsigned eye=0;eye<2;eye++){
    unsigned runs=0;bool redBefore=false;
    for(unsigned x=0;x<512;x++){
     DWORD color=row[eye*512+x];bool red=((color>>16)&255)>230&&(color&255)<20;
     if(red){centre[eye]+=x;widths[eye]++;}
     else check((color&0xffffff)==0x204080,"background complete along weapon silhouette, no stretched rim");
     if(red&&!redBefore)runs++;redBefore=red;
    }
    check(runs==1&&widths[eye]>5,"one solid weapon silhouette even with material alpha zero");centre[eye]/=widths[eye];
   }
   double disparity=centre[0]-centre[1];double expected=4.851*focal/(distance*relocation)*256;
   check(fabs(disparity-expected)<1.5,"true physical eye separation, no near disparity cap");
   check(fabs(widths[0]-2*focal/distance*256)<2,"GPS relocation preserves original angular size");
   check(fabs((centre[0]+centre[1])*.5-255.5)<1,"GPS relocation preserves original screen position");
   check(fabs(widths[0]/disparity-2*relocation/4.851)<.08,"native weapon scale or GPS stereo relocation has correct physical ratio");
   printf("z=%.0f relocation=%.0f width=%u disparity=%.2f size/depth=%.3f\n",distance,relocation,widths[0],disparity,widths[0]/disparity);
   sys->UnlockRect();
   check(weaponlayer::SourceRemovalReady(),"source removal only after actual geometry capture");
   if(distance>=20){
    D3DLOCKED_RECT zlock{};hr(worldZ->LockRect(0,&zlock,nullptr,0),"world depth fixture lock");
    for(unsigned y=0;y<512;y++){float* row=(float*)((BYTE*)zlock.pBits+y*zlock.Pitch);
     for(unsigned x=0;x<1024;x++)row[x]=(x%512)<centre[x/512]?0.f:1.f;
    }worldZ->UnlockRect(0);
    d->ColorFill(back,nullptr,0xff204080);weaponlayer::Composite(d,worldZ);
    hr(d->GetRenderTargetData(back,sys),"world occlusion readback");sys->LockRect(&lock,nullptr,D3DLOCK_READONLY);
    row=(const DWORD*)((BYTE*)lock.pBits+256*lock.Pitch);
    for(unsigned eye=0;eye<2;eye++){
     unsigned visible=0;
     for(unsigned x=0;x<512;x++){
      bool red=((row[eye*512+x]>>16)&255)>230&&(row[eye*512+x]&255)<20;
      if(x<centre[eye])check(!red,"closer world surface occludes weapon in the same eye pixel");
      if(red)visible++;
     }
     check(visible>widths[eye]/2-2&&visible<widths[eye]/2+2,"far world retains unoccluded weapon half");
    }sys->UnlockRect();
   }
   weaponlayer::EndFrame();
   d->ColorFill(back,nullptr,0xff204080);weaponlayer::Composite(d);hr(d->GetRenderTargetData(back,sys),"empty next frame");
   sys->LockRect(&lock,nullptr,D3DLOCK_READONLY);check(((DWORD*)((BYTE*)lock.pBits+256*lock.Pitch))[256]==0xff204080,"no previous weapon frame in pause/loading");sys->UnlockRect();
   weaponlayer::EndFrame();check(!weaponlayer::SourceRemovalReady(),"missing model/capture frame restores the original renderer");
  }
  if(vs)vs->Release();
 }
 HdrChain(d,back,originalDepth,sys,vb,ib,decl);
 PlasmidSubset(d,back,originalDepth,sys,decl,ps);
 d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetStreamSource(0,nullptr,0,0);d->SetIndices(nullptr);d->SetVertexDeclaration(nullptr);
 if(argc>1){
  FILE* manifest=fopen(argv[1],"r");check(manifest!=nullptr,"native shader cache manifest");char path[1024];unsigned accepted=0,guarded=0,originalUnsupported=0;
  while(fgets(path,sizeof(path),manifest)){
   path[strcspn(path,"\r\n")]=0;auto code=load(path);IDirect3DVertexShader9 *native=nullptr,*extended=nullptr;
   if(FAILED(d->CreateVertexShader(code.data(),&native))){originalUnsupported++;continue;}
   auto patched=vertexstereo::Patch(code.data(),code.size());
   if(patched.error)guarded++;
   else {HRESULT result=d->CreateVertexShader(patched.code.data(),&extended);if(FAILED(result))fprintf(stderr,"native shader failed after extension: %s\n",path);hr(result,"extend supported original BioShock vertex program");extended->Release();accepted++;}
   native->Release();
  }
  fclose(manifest);check(accepted>100,"actual game vertex shaders validated");
  printf("Native BioShock cache: %u extended, %u safely guarded, %u unsupported originals\n",accepted,guarded,originalUnsupported);
 }
 d->SetRenderTarget(1,nullptr);if(mrt)mrt->Release();back->Release();originalDepth->Release();sys->Release();vb->Release();ib->Release();ps->Release();decl->Release();
 worldZ->Release();weaponlayer::Release();check(!weaponlayer::SourceRemovalReady(),"Reset disables previous model source removal");hr(d->Reset(&pp),"device Reset releases all geometry-layer resources");
 d->Release();api->Release();DestroyWindow(w);
 puts("PASS: mesh dispatch, indexed/ordinary, VS2/3/fixed geometry stereo, physical size across distances, near plane, zero-alpha materials, clean background, state/MRT, stale frames, Reset");
}
