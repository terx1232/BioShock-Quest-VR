#include "gameplayhud.h"
#include "d3dguard.h"
#include "log.h"
#include <cmath>
#include <cstring>

namespace gameplayhud {
namespace {
using vr::Vec3;
IDirect3DStateBlock9* state=nullptr;
IDirect3DDevice9* owner=nullptr;
bool logged=false;
struct TextAnchor {bool valid=false;unsigned resumes=0;Vec3 center{},right{},up{},forward{};} textAnchor;
struct UiVertex {float x,y,z,u,v;};
struct UiBatch {
    UiVertex vertices[1024];unsigned count=0;
    bool Triangle(UiVertex a,UiVertex b,UiVertex c){if(count+3>1024)return false;vertices[count++]=a;vertices[count++]=b;vertices[count++]=c;return true;}
    bool Quad(const UiVertex* q){return Triangle(q[0],q[1],q[2])&&Triangle(q[2],q[1],q[3]);}
    bool Strip(const UiVertex* q,unsigned n){for(unsigned i=0;i+2<n;i++)if(!Triangle(q[i+(i&1)],q[i+1-(i&1)],q[i+2]))return false;return true;}
    bool Draw(IDirect3DDevice9* d){return !count||SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,count/3,vertices,sizeof(UiVertex)));}
} batch;
float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
Vec3 Norm(Vec3 a){float n=sqrtf(Dot(a,a));return n>1e-6f?a*(1/n):Vec3{1,0,0};}
void View(IDirect3DDevice9* d,const vr::Mat3& h,Vec3 eye,float fov,float aspect){
    Vec3 x=vr::Column(h,0),y=vr::Column(h,1),z=vr::Column(h,2);
    D3DMATRIX v={{{x.x,y.x,z.x,0,x.y,y.y,z.y,0,x.z,y.z,z.z,0,-Dot(x,eye),-Dot(y,eye),-Dot(z,eye),1}}};
    float xs=1/tanf(fov*vr::kPi/360),zn=.05f,zf=100;
    D3DMATRIX p={{{xs,0,0,0,0,xs*aspect,0,0,0,0,zf/(zn-zf),-1,0,0,zn*zf/(zn-zf),0}}};
    d->SetTransform(D3DTS_VIEW,&v);d->SetTransform(D3DTS_PROJECTION,&p);
}
bool Quad(IDirect3DDevice9* d,Vec3 c,Vec3 right,Vec3 up,float width,float height,hudlayout::Rect uv){
    using V=UiVertex;Vec3 x=right*(width*.5f),y=up*(height*.5f);
    Vec3 a=c-x+y,b=c+x+y,e=c-x-y,f=c+x-y;
    V q[]={{a.x,a.y,a.z,uv.x0,uv.y0},{b.x,b.y,b.z,uv.x1,uv.y0},
           {e.x,e.y,e.z,uv.x0,uv.y1},{f.x,f.y,f.z,uv.x1,uv.y1}};
    return batch.Quad(q);
}
bool Wrist(IDirect3DDevice9* d,const xrapi::Hand& hand,Vec3 head,Vec3 headRight,
           hudlayout::Rect r,const hudlayout::Source& s,float width,bool left,float vertical=0,float horizontal=0){
    if(r.x1<=r.x0||r.y1<=r.y0)return true;
    vr::Mat3 grip=vr::FromQuat(hand.grip);
    Vec3 center=hand.position+vr::Column(grip,1)*.075f+vr::Column(grip,2)*.11f;
    if(!left)center=center+vr::Column(grip,0)*.06f;
    Vec3 normal=Norm(head-center),right=Cross(Vec3{0,1,0},normal);
    if(Dot(right,right)<.001f)right=headRight;
    right=Norm(right);Vec3 up=Norm(Cross(normal,right));
    center=center+up*vertical+right*horizontal;
    float height=width*((r.y1-r.y0)*s.height)/((r.x1-r.x0)*s.width);
    return Quad(d,center,right,up,width,height,hudlayout::Uv(s,r));
}
bool CurvedText(IDirect3DDevice9* d,const TextAnchor& a,float width,float height,hudlayout::Rect uv){
    UiVertex strip[34];
    for(unsigned i=0;i<=16;i++){
        float t=float(i)/16,x=t*2-1;Vec3 c=a.center+a.right*(x*width*.5f)+a.forward*(.08f*x*x);
        for(unsigned j=0;j<2;j++){
            Vec3 p=c+a.up*((j?-.5f:.5f)*height);
            strip[2*i+j]={p.x,p.y,p.z,uv.x0+(uv.x1-uv.x0)*t,j?uv.y1:uv.y0};
        }
    }
    return batch.Strip(strip,34);
}
}
bool Render(IDirect3DDevice9* d,IDirect3DTexture9* tex,const hudlayout::Source& src,
            const hudlayout::Layout& layout,const xrapi::State& pose,float fov,int views,float scale,Options options){
    if(!d||!tex||!src.Valid()||!pose.valid||(views!=1&&views!=2)||!std::isfinite(fov)||fov<30||fov>150)return false;
    D3DSURFACE_DESC td{};if(FAILED(tex->GetLevelDesc(0,&td))||td.Width!=src.textureWidth||td.Height!=src.textureHeight)return false;
    if(owner!=d){Release();owner=d;}
    if(!state&&FAILED(d->CreateStateBlock(D3DSBT_ALL,&state)))return false;
    TargetsGuard targets(d);if(FAILED(state->Capture()))return false;
    IDirect3DSurface9* back=nullptr;
    if(FAILED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back)))return false;
    D3DSURFACE_DESC desc{};back->GetDesc(&desc);
    d->SetDepthStencilSurface(nullptr);for(unsigned i=0;i<targets.count;i++)d->SetRenderTarget(i+1,nullptr);
    d->SetRenderTarget(0,back);back->Release();
    d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetFVF(D3DFVF_XYZ|D3DFVF_TEX1);
    D3DMATRIX identity={{{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}}};
    d->SetTransform(D3DTS_WORLD,&identity); // GameHands=0 must not inherit the last world mesh transform.
    d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    d->SetRenderState(D3DRS_LIGHTING,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    d->SetRenderState(D3DRS_FOGENABLE,FALSE);d->SetRenderState(D3DRS_STENCILENABLE,FALSE);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_CLIPPLANEENABLE,0);
    d->SetRenderState(D3DRS_VERTEXBLEND,D3DVBF_DISABLE);d->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE,FALSE);
    d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);d->SetRenderState(D3DRS_COLORWRITEENABLE,0x7);
    d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
    d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);
    d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    d->SetTexture(0,tex);d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);
    d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
    d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
    vr::Mat3 head=vr::FromQuat(pose.hmdOrientation);Vec3 right=vr::Column(head,0),up=vr::Column(head,1),forward=vr::Column(head,2)*-1;
    const float frontDistance=1.6f;
    Vec3 front=pose.hmdPosition+forward*frontDistance;
    float ipd=pose.ipd>.04f&&pose.ipd<.09f?pose.ipd:.063f;
    float wristScale=std::clamp(scale,.5f,3.f)/1.5f;
    // Restore the former near-full-eye text size. The old 2.3m / 2m panel
    // shrank a 1728px Flash canvas to only ~417px in an 864px eye: small
    // prompts lost their strokes. Keep the complete canvas and its aspect,
    // with a margin for stereo disparity instead of cropping its middle.
    float frontW=2*frontDistance*tanf(fov*vr::kPi/360)*.92f;
    float frontH=frontW*src.height/src.width;
    TextAnchor focus{true,pose.resumes,pose.hmdPosition+forward*1.25f-up*.03f,right,up,forward};
    if(!options.loot)textAnchor.valid=false;
    if(options.loot){
        if(!textAnchor.valid||textAnchor.resumes!=pose.resumes||Dot(textAnchor.center-pose.hmdPosition,textAnchor.center-pose.hmdPosition)>16)textAnchor=focus;
        focus=textAnchor;
    }
    bool ok=SUCCEEDED(d->BeginScene());
    if(ok){
        for(int eye=0;eye<views;eye++){
            batch.count=0;
            D3DVIEWPORT9 vp{unsigned(eye)*desc.Width/unsigned(views),0,desc.Width/unsigned(views),desc.Height,0,1};d->SetViewport(&vp);
            Vec3 eyePos=pose.hmdPosition+right*(views==2?(eye==0?-.5f:.5f)*ipd:0);
            View(d,head,eyePos,fov,float(vp.Width)/vp.Height);
            // Identical physical panel geometry for both eyes; only the view changes.
            for(unsigned i=0;i<layout.count;i++){
                auto r=layout.front[i];Vec3 c=front+right*((r.x0+r.x1-1)*frontW*.5f)+up*((1-r.y0-r.y1)*frontH*.5f);
                ok=Quad(d,c,right,up,(r.x1-r.x0)*frontW,(r.y1-r.y0)*frontH,hudlayout::Uv(src,r))&&ok;
            }
            if(layout.vrText){
                auto r=layout.focus;float w=2.15f,h=w*(r.y1-r.y0)*src.height/((r.x1-r.x0)*src.width);
                ok=CurvedText(d,focus,w,h,hudlayout::Uv(src,r))&&ok;
            }
            if(layout.messagesLeft)
                ok=Wrist(d,pose.left,pose.hmdPosition,right,layout.messages,src,.60f*wristScale,true,.35f*wristScale,0)&&ok;
            if(layout.dashboard){
                ok=Wrist(d,pose.left,pose.hmdPosition,right,layout.health,src,.24f*wristScale,true,.085f*wristScale,-.10f*wristScale)&&ok;
                ok=Wrist(d,pose.left,pose.hmdPosition,right,layout.resources,src,.15f*wristScale,true,.085f*wristScale,.13f*wristScale)&&ok;
                ok=Wrist(d,pose.left,pose.hmdPosition,right,layout.inventory,src,.24f*wristScale,true,-.10f*wristScale,-.10f*wristScale)&&ok;
                ok=Wrist(d,pose.left,pose.hmdPosition,right,layout.ammo,src,.20f*wristScale,true,-.10f*wristScale,.13f*wristScale)&&ok;
            }else{
            ok=Wrist(d,pose.left,pose.hmdPosition,right,layout.health,src,.24f*wristScale,true)&&ok;
            auto physicalHeight=[&](hudlayout::Rect r,float width){return r.x1>r.x0?width*(r.y1-r.y0)*src.height/((r.x1-r.x0)*src.width):0.f;};
            float gap=(physicalHeight(layout.health,.24f)+physicalHeight(layout.ammo,.21f))*.5f+.025f;
            ok=Wrist(d,options.ammoOnLeft?pose.left:pose.right,pose.hmdPosition,right,layout.ammo,src,.21f*wristScale,options.ammoOnLeft,options.ammoOnLeft?-gap*wristScale:0)&&ok;
            }
            ok=batch.Draw(d)&&ok;
        }
        d->EndScene();
    }
    state->Apply();
    if(ok&&!logged){logged=true;Log("Gameplay HUD: dashboard=%d (left HP/EVE, ammo, equipment, ADAM/credits), VR text=%d at 1.25m, left quest/messages=%d; native GPS stays in the game geometry; source %u,%u %ux%u in %ux%u, %d eyes",layout.dashboard,layout.vrText,layout.messagesLeft,
        src.x,src.y,src.width,src.height,src.textureWidth,src.textureHeight,views);}
    return ok;
}
void Release(){if(state)state->Release();state=nullptr;owner=nullptr;logged=false;textAnchor={};}
}
