#include "weaponlayer.h"
#include "handmask.h"
#include "vertexstereo.h"
#include "samplermask.h"
#include "config.h"
#include "d3dguard.h"
#include "log.h"
#include "vrframe.h"
#include "theater.h"
#include <cmath>
#include <vector>

namespace weaponlayer {
namespace {
#include "stereoshaders.inc"
struct Shader {IDirect3DVertexShader9* original=nullptr;IDirect3DVertexShader9* stereo=nullptr;unsigned constant=0,sampler=0;bool textureParams=false;};
struct Node {
    IDirect3DSurface9* original=nullptr;
    IDirect3DTexture9* originalTexture=nullptr;
    IDirect3DTexture9* image[2]={};
    IDirect3DSurface9* target[2]={};
    D3DSURFACE_DESC desc{};
    unsigned epoch=0;
};
std::vector<Shader> shaders;
struct PixelInputs {IDirect3DPixelShader9* program;unsigned mask;};
std::vector<PixelInputs> pixelInputs;
ReplayWork replayWork{};
std::vector<Node> nodes;
IDirect3DDevice9* device=nullptr;
IDirect3DTexture9* depth[2]={};
IDirect3DTexture9* eyeParams[2]={};
float cachedEyeParams[2][4]={};
IDirect3DSurface9* zbuffer[2]={};
IDirect3DVertexShader9* quadVS=nullptr;
IDirect3DPixelShader9* quadPS=nullptr;
IDirect3DVertexDeclaration9* quadDecl=nullptr;
IDirect3DStateBlock9* state=nullptr;
unsigned width=0,height=0,epoch=1,sourceW=0,sourceH=0;
Node *baseNode=nullptr,*finalNode=nullptr;
bool haveFrame=false,unavailable=false,geometryLogged=false,fixedLogged=false;
bool transformedLogged=false,occlusionLogged=false,toneLogged=false,completed=false;
bool displayedThisFrame=false;
IDirect3DIndexBuffer9 *handOriginal=nullptr,*handCropped=nullptr;
unsigned handStart=0,handMin=0,handFaces=0;bool handAttempted=false;
struct ModDraw {bool saved;ModDraw():saved(theater::g_modDraw){theater::g_modDraw=true;}~ModDraw(){theater::g_modDraw=saved;}};

bool Ensure(IDirect3DDevice9* d){
    if(device&&device!=d)Release();device=d;if(unavailable)return false;if(state)return true;
    IDirect3DSurface9* back=nullptr;D3DSURFACE_DESC desc{};
    if(FAILED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back)))return false;
    back->GetDesc(&desc);back->Release();width=desc.Width;height=desc.Height;
    const D3DVERTEXELEMENT9 declaration[]={
        {0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    bool ok=SUCCEEDED(d->CreateVertexShader(kFullscreenVs,&quadVS))&&
        SUCCEEDED(d->CreatePixelShader(kWeaponlayerPS,&quadPS))&&
        SUCCEEDED(d->CreateStateBlock(D3DSBT_ALL,&state))&&
        SUCCEEDED(d->CreateVertexDeclaration(declaration,&quadDecl));
    for(unsigned eye=0;ok&&eye<2;eye++)ok=
        SUCCEEDED(d->CreateTexture(width,height,1,D3DUSAGE_DEPTHSTENCIL,(D3DFORMAT)MAKEFOURCC('I','N','T','Z'),D3DPOOL_DEFAULT,&depth[eye],nullptr))&&
        SUCCEEDED(depth[eye]->GetSurfaceLevel(0,&zbuffer[eye]));
    if(!ok){Release();device=d;unavailable=true;Log("Weapon stereo: allocation failed; original rendering retained until Reset");return false;}
    nodes.reserve(24);Log("Weapon stereo: two geometry views, native HDR postprocessing, frame %ux%u",width,height);return true;
}
Node* GetNode(IDirect3DDevice9* d,IDirect3DSurface9* original){
    for(auto& n:nodes)if(n.original==original)return &n;
    if(!original||nodes.size()>=24)return nullptr;
    Node n;if(FAILED(original->GetDesc(&n.desc)))return nullptr;
    if(n.desc.Format!=D3DFMT_A8R8G8B8&&n.desc.Format!=D3DFMT_X8R8G8B8&&n.desc.Format!=D3DFMT_A16B16G16R16F)return nullptr;
    if(n.desc.Width<32||n.desc.Height<32)return nullptr; // retain the world's exposure
    for(unsigned eye=0;eye<2;eye++){
        if(FAILED(d->CreateTexture(n.desc.Width,n.desc.Height,1,D3DUSAGE_RENDERTARGET,n.desc.Format,D3DPOOL_DEFAULT,&n.image[eye],nullptr))||
           FAILED(n.image[eye]->GetSurfaceLevel(0,&n.target[eye]))){
            for(unsigned k=0;k<2;k++){if(n.target[k])n.target[k]->Release();if(n.image[k])n.image[k]->Release();}
            unavailable=true;Log("Weapon stereo: postprocess allocation failed; original rendering restored until Reset");
            return nullptr;
        }
    }
    original->AddRef();n.original=original;original->GetContainer(IID_IDirect3DTexture9,(void**)&n.originalTexture);
    nodes.push_back(n);return &nodes.back();
}
Node* Sampled(IDirect3DBaseTexture9* texture){
    if(!texture)return nullptr;
    for(auto& n:nodes)if(n.epoch==epoch&&n.originalTexture==texture)return &n;
    return nullptr;
}
Shader* Find(IDirect3DDevice9* d,IDirect3DVertexShader9* original){
    for(auto& s:shaders)if(s.original==original)return &s;if(shaders.size()>=64)return nullptr;
    Shader s;s.original=original;original->AddRef();UINT bytes=0;original->GetFunction(nullptr,&bytes);
    if(!bytes||bytes>65536||bytes%4){shaders.push_back(s);return &shaders.back();}
    std::vector<DWORD> code(bytes/4);original->GetFunction(code.data(),&bytes);
    auto p=vertexstereo::Patch(code.data(),code.size());s.constant=p.constant;s.sampler=p.sampler;s.textureParams=p.textureParams;
    HRESULT hr=p.error?D3DERR_INVALIDCALL:d->CreateVertexShader(p.code.data(),&s.stereo);
    if(FAILED(hr))Log("Weapon stereo: shader fallback, %s hr=%08lx",p.error?p.error:"driver rejected program",hr);
    else Log("Weapon stereo: vertex program extended, vs=%u temp=%u constant=%u bone-safe texture=%d",D3DSHADER_VERSION_MAJOR(code[0]),p.temp,p.constant,p.textureParams);
    shaders.push_back(s);return &shaders.back();
}
unsigned Samplers(IDirect3DDevice9* d){
    IDirect3DPixelShader9* ps=nullptr;
    if(FAILED(d->GetPixelShader(&ps))||!ps){if(ps)ps->Release();return samplermask::all;}
    for(const auto& item:pixelInputs)if(item.program==ps){ps->Release();return item.mask;}
    if(pixelInputs.size()>=256){ps->Release();return samplermask::all;}
    unsigned mask=samplermask::all;UINT bytes=0;
    if(SUCCEEDED(ps->GetFunction(nullptr,&bytes))&&bytes>=8&&bytes<=65536&&!(bytes%4)){
        std::vector<DWORD> code(bytes/4);UINT actual=bytes;
        if(SUCCEEDED(ps->GetFunction(code.data(),&actual))&&actual==bytes)mask=samplermask::Read(code.data(),code.size());
    }
    // Hold a reference so a destroyed shader's address cannot give another
    // program a stale mask. Bound the cache; an overflow is still correct.
    pixelInputs.push_back({ps,mask});
    Log("Weapon postprocess: cached sampler mask %04x; conservative native-chain fallback retained",mask);
    return mask;
}
}

static bool DrawMesh(IDirect3DDevice9* d,D3DPRIMITIVETYPE type,INT base,UINT minVertex,UINT vertices,UINT start,UINT count,DrawIndexedFn indexed,DrawPrimitiveFn ordinary,float relocation){
    if(!std::isfinite(relocation)||relocation<.25f||relocation>128)relocation=1;
    if(!g_config.weaponGeometry||!g_config.depthStereo||theater::g_modDraw)return false;
    const auto f=vrframe::Current();if(!f.active||!f.inGame)return false;
    DWORD colorWrite=0;d->GetRenderState(D3DRS_COLORWRITEENABLE,&colorWrite);
    if(!(colorWrite&7))return false; // depth/stencil masks cannot become opaque black geometry
    IDirect3DVertexShader9* original=nullptr;if(FAILED(d->GetVertexShader(&original)))return false;
    bool fixed=!original;D3DMATRIX view{},projection{},nearProjection{};
    if(fixed){
        DWORD fvf=0;d->GetFVF(&fvf);bool transformed=(fvf&D3DFVF_POSITION_MASK)==D3DFVF_XYZRHW;
        IDirect3DVertexDeclaration9* decl=nullptr;
        if(SUCCEEDED(d->GetVertexDeclaration(&decl))&&decl){
            D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1];UINT n=MAXD3DDECLLENGTH+1;
            if(SUCCEEDED(decl->GetDeclaration(elements,&n)))for(UINT i=0;i<n;i++)if(elements[i].Usage==D3DDECLUSAGE_POSITIONT)transformed=true;
            decl->Release();
        }
        if(transformed){if(!transformedLogged){transformedLogged=true;Log("Weapon stereo: pretransformed vertices; original retained");}return false;}
        if(FAILED(d->GetTransform(D3DTS_VIEW,&view))||FAILED(d->GetTransform(D3DTS_PROJECTION,&projection)))return false;
        nearProjection=projection;
        for(unsigned row=0;row<4;row++)nearProjection.m[row][2]=projection.m[row][3]+g_config.weaponNearScale*(projection.m[row][2]-projection.m[row][3]);
        if(!fixedLogged){fixedLogged=true;Log("Weapon stereo: fixed-function geometry, fvf=%08lx",fvf);}
    }
    if(!Ensure(d)){if(original)original->Release();return false;}
    Shader* shader=fixed?nullptr:Find(d,original);
    if(!fixed&&(!shader||!shader->stereo)){original->Release();return false;}
    TargetsGuard targets(d);ModDraw mod;
    Node* node=GetNode(d,targets.rt);
    if(!node||node->desc.Width!=width||node->desc.Height!=height){if(original)original->Release();return false;}
    baseNode=node;
    for(unsigned i=0;i<targets.count;i++)d->SetRenderTarget(i+1,nullptr);
    float savedConstant[4]={};IDirect3DBaseTexture9* savedTexture=nullptr;DWORD savedSamplers[7]={};
    const D3DSAMPLERSTATETYPE samplerStates[]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE,D3DSAMP_MAXMIPLEVEL};
    const DWORD samplerValues[]={D3DTADDRESS_CLAMP,D3DTADDRESS_CLAMP,D3DTEXF_POINT,D3DTEXF_POINT,D3DTEXF_NONE,FALSE,0};
    if(shader){
        if(shader->textureParams){
            d->GetTexture(D3DVERTEXTEXTURESAMPLER0+shader->sampler,&savedTexture);
            for(unsigned i=0;i<7;i++){
                d->GetSamplerState(D3DVERTEXTEXTURESAMPLER0+shader->sampler,samplerStates[i],&savedSamplers[i]);
                d->SetSamplerState(D3DVERTEXTEXTURESAMPLER0+shader->sampler,samplerStates[i],samplerValues[i]);
            }
        }else d->GetVertexShaderConstantF(shader->constant,savedConstant,1);
    }
    DWORD scissor=0,zEnable=0,zWrite=0,zFunc=0,stencil=0;
    d->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor);d->GetRenderState(D3DRS_ZENABLE,&zEnable);
    d->GetRenderState(D3DRS_ZWRITEENABLE,&zWrite);d->GetRenderState(D3DRS_ZFUNC,&zFunc);d->GetRenderState(D3DRS_STENCILENABLE,&stencil);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_ZENABLE,TRUE);
    d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);d->SetRenderState(D3DRS_STENCILENABLE,FALSE);
    if(shader)d->SetVertexShader(shader->stereo);else d->SetTransform(D3DTS_PROJECTION,&nearProjection);
    const float offset=f.ipdUnits*.5f/tanf(f.fov*vr::kPi/360.f);bool ok=true;
    for(unsigned eye=0;ok&&eye<2;eye++){
        d->SetDepthStencilSurface(nullptr);
        ok=SUCCEEDED(d->SetRenderTarget(0,node->target[eye]))&&SUCCEEDED(d->SetDepthStencilSurface(zbuffer[eye]));
        if(ok&&node->epoch!=epoch){D3DVIEWPORT9 full={0,0,width,height,0,1};d->SetViewport(&full);ok=SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0,1,0));}
        d->SetViewport(&targets.vp);
        if(shader){
            const float c[4]={eye?-offset:offset,g_config.weaponNearScale,relocation,0};
            if(shader->textureParams){
                if(ok&&!eyeParams[eye])ok=SUCCEEDED(d->CreateTexture(1,1,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&eyeParams[eye],nullptr));
                if(ok&&memcmp(cachedEyeParams[eye],c,sizeof(c))){
                    D3DLOCKED_RECT lock{};ok=SUCCEEDED(eyeParams[eye]->LockRect(0,&lock,nullptr,0));
                    if(ok){memcpy(lock.pBits,c,sizeof(c));ok=SUCCEEDED(eyeParams[eye]->UnlockRect(0));if(ok)memcpy(cachedEyeParams[eye],c,sizeof(c));}
                }
                if(ok)ok=SUCCEEDED(d->SetTexture(D3DVERTEXTEXTURESAMPLER0+shader->sampler,eyeParams[eye]));
            }else d->SetVertexShaderConstantF(shader->constant,c,1);
        }
        else {D3DMATRIX v=view;for(unsigned r=0;r<4;r++)for(unsigned c=0;c<3;c++)v.m[r][c]*=relocation;v._41+=(eye?-.5f:.5f)*f.ipdUnits;d->SetTransform(D3DTS_VIEW,&v);}
        if(ok)ok=SUCCEEDED(indexed?indexed(d,type,base,minVertex,vertices,start,count):ordinary(d,type,start,count));
    }
    if(shader){
        d->SetVertexShader(original);
        if(shader->textureParams){
            d->SetTexture(D3DVERTEXTEXTURESAMPLER0+shader->sampler,savedTexture);if(savedTexture)savedTexture->Release();
            for(unsigned i=0;i<7;i++)d->SetSamplerState(D3DVERTEXTEXTURESAMPLER0+shader->sampler,samplerStates[i],savedSamplers[i]);
        }else d->SetVertexShaderConstantF(shader->constant,savedConstant,1);
        original->Release();
    }
    else {d->SetTransform(D3DTS_VIEW,&view);d->SetTransform(D3DTS_PROJECTION,&projection);}
    d->SetRenderState(D3DRS_SCISSORTESTENABLE,scissor);d->SetRenderState(D3DRS_ZENABLE,zEnable);
    d->SetRenderState(D3DRS_ZWRITEENABLE,zWrite);d->SetRenderState(D3DRS_ZFUNC,zFunc);d->SetRenderState(D3DRS_STENCILENABLE,stencil);
    if(!ok)return false;
    node->epoch=epoch;haveFrame=true;
    sourceW=g_config.gameWidth?g_config.gameWidth:targets.vp.Width;sourceH=g_config.gameHeight?g_config.gameHeight:targets.vp.Height;
    if(node->desc.Format!=D3DFMT_A16B16G16R16F)finalNode=node;
    if(!geometryLogged){geometryLogged=true;Log("Weapon stereo: first geometry pair, %u vertices, physical IPD %.3f, source format %u",vertices,f.ipdUnits,node->desc.Format);}
    // A successful capture owns this draw in the current frame, including
    // the first HDR frame after a model switch. Keeping the mono source here
    // flashes a second weapon into the depth-reconstructed world while the
    // new eye pair is already being composed. An incomplete tone chain may
    // delay the model, but must never publish that temporary mono copy.
    // Failures before successful capture still return false above.
    return true;
}
bool DrawIndexed(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,INT b,UINT m,UINT v,UINT s,UINT c,DrawIndexedFn fn,float relocation){return DrawMesh(d,t,b,m,v,s,c,fn,nullptr,relocation);}
bool DrawPrimitive(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT s,UINT c,DrawPrimitiveFn fn,float relocation){return DrawMesh(d,t,0,0,0,s,c,nullptr,fn,relocation);}
bool DrawPlasmidHand(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,INT base,UINT min,UINT vertices,UINT start,UINT faces,DrawIndexedFn draw){
    if(t!=D3DPT_TRIANGLELIST||vertices!=handmaskdata::vertices||faces!=handmaskdata::triangles)return false;
    IDirect3DIndexBuffer9* source=nullptr;if(FAILED(d->GetIndices(&source))||!source)return false;
    if(source!=handOriginal||start!=handStart||min!=handMin){
        if(handOriginal)handOriginal->Release();if(handCropped)handCropped->Release();
        handOriginal=source;source->AddRef();handCropped=nullptr;handStart=start;handMin=min;handFaces=0;handAttempted=false;
    }
    if(!handAttempted){
        handAttempted=true;D3DINDEXBUFFER_DESC desc{};source->GetDesc(&desc);
        unsigned stride=desc.Format==D3DFMT_INDEX16?2:desc.Format==D3DFMT_INDEX32?4:0;
        void* data=nullptr;unsigned bytes=faces*3*stride;std::vector<unsigned char> selected;
        bool valid=stride&&std::uint64_t(start+faces*3)*stride<=desc.Size&&SUCCEEDED(source->Lock(start*stride,bytes,&data,D3DLOCK_READONLY));
        if(valid){
            if(stride==2){std::vector<unsigned short> indices;valid=handmask::Select((unsigned short*)data,faces*3,min,indices);selected.resize(indices.size()*2);if(valid)memcpy(selected.data(),indices.data(),selected.size());}
            else {std::vector<unsigned> indices;valid=handmask::Select((unsigned*)data,faces*3,min,indices);selected.resize(indices.size()*4);if(valid)memcpy(selected.data(),indices.data(),selected.size());}
            source->Unlock();
        }
        if(valid){
            valid=SUCCEEDED(d->CreateIndexBuffer(unsigned(selected.size()),D3DUSAGE_WRITEONLY,desc.Format,D3DPOOL_MANAGED,&handCropped,nullptr));
            if(valid){valid=SUCCEEDED(handCropped->Lock(0,0,&data,0));if(valid){memcpy(data,selected.data(),selected.size());valid=SUCCEEDED(handCropped->Unlock());}}
        }
        if(valid){handFaces=handmaskdata::palmTriangles;Log("Plasmid geometry: native topology verified, %u/%u triangles retained; left palm/fingers only",handFaces,faces);}
        else {if(handCropped)handCropped->Release();handCropped=nullptr;Log("Plasmid geometry: hand index verification/readback unavailable; arms remain hidden, native effects retained");}
    }
    bool captured=false;
    if(handCropped){d->SetIndices(handCropped);captured=DrawMesh(d,t,base,min,vertices,0,handFaces,draw,nullptr,1);d->SetIndices(source);}
    source->Release();return captured;
}
bool SourceRemovalReady(){return completed&&state&&!unavailable;}

void ReplayPostProcess(IDirect3DDevice9* d,D3DPRIMITIVETYPE type,UINT count,const void* data,UINT stride,DrawUPFn draw){
    if(unavailable||!haveFrame||!baseNode||theater::g_modDraw||!data||!draw)return;
    const unsigned mask=Samplers(d);
    replayWork.calls++;
    Node* inputs[16]={};bool any=false;
    for(unsigned s=0;s<16;s++)if(mask&(1u<<s)){
        replayWork.textureReads++;
        IDirect3DBaseTexture9* texture=nullptr;d->GetTexture(s,&texture);inputs[s]=Sampled(texture);
        if(texture)texture->Release();any|=inputs[s]!=nullptr;
    }
    if(!any){
        // Preserve the old chain's behaviour for constant/partial passes that
        // leave a tracked texture in an unused slot. They can still update an
        // existing output node; dropping their replay would leave stale pixels.
        for(unsigned s=0;s<16;s++)if(!(mask&(1u<<s))){
            replayWork.textureReads++;
            IDirect3DBaseTexture9* texture=nullptr;d->GetTexture(s,&texture);
            bool tracked=Sampled(texture)!=nullptr;if(texture)texture->Release();
            if(tracked){any=true;break;}
        }
    }
    if(!any)return;
    TargetsGuard targets(d);Node* output=GetNode(d,targets.rt);if(!output)return;
    ModDraw mod;state->Capture();
    for(unsigned i=0;i<targets.count;i++)d->SetRenderTarget(i+1,nullptr);
    d->SetDepthStencilSurface(nullptr);bool ok=true;
    for(unsigned eye=0;ok&&eye<2;eye++){
        ok=SUCCEEDED(d->SetRenderTarget(0,output->target[eye]));
        if(output->epoch!=epoch){
            D3DVIEWPORT9 full={0,0,output->desc.Width,output->desc.Height,0,1};d->SetViewport(&full);
            if(ok)ok=SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
        }
        d->SetViewport(&targets.vp);
        for(unsigned s=0;s<16;s++)if(inputs[s])d->SetTexture(s,inputs[s]->image[eye]);
        if(ok){replayWork.replayedDraws++;ok=SUCCEEDED(draw(d,type,count,data,stride));}
    }
    state->Apply();
    if(!ok)return;output->epoch=epoch;
    IDirect3DSurface9* back=nullptr;d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back);
    if(targets.rt==back&&output->desc.Format!=D3DFMT_A16B16G16R16F){
        finalNode=output;
        if(!toneLogged){toneLogged=true;Log("Weapon stereo: native HDR-to-LDR chain complete, same game shaders/exposure/UVs");}
    }
    if(back)back->Release();
}
ReplayWork TakeReplayWork(){auto work=replayWork;replayWork={};return work;}

void Composite(IDirect3DDevice9* d,IDirect3DTexture9* worldDepth){
    if(unavailable||!haveFrame||!finalNode||finalNode->epoch!=epoch||!state)return;
    TargetsGuard targets(d);ModDraw mod;state->Capture();
    for(unsigned i=0;i<targets.count;i++)d->SetRenderTarget(i+1,nullptr);
    IDirect3DSurface9* back=nullptr;d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back);
    d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,back);if(back)back->Release();
    d->SetVertexDeclaration(quadDecl);d->SetVertexShader(quadVS);d->SetPixelShader(quadPS);
    d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_CLIPPLANEENABLE,0);
    d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
    d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
    d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    d->SetRenderState(D3DRS_COLORWRITEENABLE,0xf);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    d->SetTexture(2,worldDepth);
    const float p[4]={g_config.weaponNearScale,worldDepth?1.f:0.f,0,0};d->SetPixelShaderConstantF(0,p,1);
    for(unsigned s=0;s<3;s++){
        d->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
        d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE);
    }
    struct V {float x,y,z,w,u,v;};float hx=2.f/width,hy=1.f/height;
    V q[4]={{-1-hx,1+hy,0,1,0,0},{1-hx,1+hy,0,1,1,0},{-1-hx,-1+hy,0,1,0,1},{1-hx,-1+hy,0,1,1,1}};
    bool ok=SUCCEEDED(d->BeginScene());
    if(ok){
        for(unsigned eye=0;ok&&eye<2;eye++){
            D3DVIEWPORT9 vp={eye*(width/2),0,width/2,height,0,1};d->SetViewport(&vp);
            d->SetTexture(0,finalNode->image[eye]);d->SetTexture(1,depth[eye]);
            const float map[4]={(float)sourceW/finalNode->desc.Width,(float)sourceH/finalNode->desc.Height,eye*.5f,0};
            d->SetPixelShaderConstantF(1,map,1);
            ok=SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V)));
        }
        ok=SUCCEEDED(d->EndScene())&&ok;
    }
    state->Apply();
    if(ok){
        if(!completed)Log("Weapon stereo: complete per-eye model displayed");
        completed=displayedThisFrame=true;
        if(worldDepth&&!occlusionLogged){occlusionLogged=true;Log("Weapon stereo: world occlusion active, same per-eye scene depth");}
    }
}
void EndFrame(){
    if(completed&&!displayedThisFrame){completed=false;Log("Weapon stereo: incomplete postprocess frame; completed image invalidated");}
    displayedThisFrame=false;haveFrame=false;baseNode=finalNode=nullptr;
    if(++epoch==0){epoch=1;for(auto& n:nodes)n.epoch=0;}
}
void InvalidateModel(){
    completed=displayedThisFrame=haveFrame=false;geometryLogged=false;baseNode=finalNode=nullptr;
    if(++epoch==0){epoch=1;for(auto& n:nodes)n.epoch=0;}
}
void Release(){
    if(handOriginal)handOriginal->Release();if(handCropped)handCropped->Release();handOriginal=handCropped=nullptr;handAttempted=false;handFaces=0;
    for(auto& s:shaders){if(s.original)s.original->Release();if(s.stereo)s.stereo->Release();}shaders.clear();
    for(const auto& item:pixelInputs)item.program->Release();pixelInputs.clear();
    replayWork={};
    for(auto& n:nodes){
        if(n.original)n.original->Release();if(n.originalTexture)n.originalTexture->Release();
        for(unsigned eye=0;eye<2;eye++){if(n.target[eye])n.target[eye]->Release();if(n.image[eye])n.image[eye]->Release();}
    }nodes.clear();
    for(unsigned eye=0;eye<2;eye++){if(zbuffer[eye])zbuffer[eye]->Release();if(depth[eye])depth[eye]->Release();zbuffer[eye]=nullptr;depth[eye]=nullptr;}
    for(unsigned eye=0;eye<2;eye++){if(eyeParams[eye])eyeParams[eye]->Release();eyeParams[eye]=nullptr;memset(cachedEyeParams[eye],0,sizeof(cachedEyeParams[eye]));}
    if(quadVS)quadVS->Release();if(quadPS)quadPS->Release();if(quadDecl)quadDecl->Release();if(state)state->Release();
    device=nullptr;quadVS=nullptr;quadPS=nullptr;quadDecl=nullptr;state=nullptr;
    width=height=sourceW=sourceH=0;epoch=1;baseNode=finalNode=nullptr;
    haveFrame=unavailable=geometryLogged=fixedLogged=transformedLogged=occlusionLogged=toneLogged=completed=displayedThisFrame=false;
}
}

