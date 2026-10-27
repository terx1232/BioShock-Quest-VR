// Geometry only: UI coordinates belong to the engine canvas, not the SBS buffer.
#pragma once
#include <algorithm>
#include <cmath>

namespace hudlayout {
struct Rect { float x0=0,y0=0,x1=0,y1=0; };
struct Source {
    unsigned x=0,y=0,width=0,height=0,textureWidth=0,textureHeight=0;
    bool Valid() const {return width&&height&&textureWidth&&textureHeight&&
        x<textureWidth&&y<textureHeight&&width<=textureWidth-x&&height<=textureHeight-y;}
};
inline Source Observe(unsigned tw,unsigned th,unsigned x,unsigned y,unsigned vw,unsigned vh){
    if(x>=tw||y>=th)return {};
    unsigned w=std::min(vw,tw-x),h=std::min(vh,th-y);
    return {x,y,w,h,tw,th};
}
inline Rect Clamp(Rect r){
    auto c=[](float v){return std::isfinite(v)?std::clamp(v,0.f,1.f):0.f;};
    r={c(r.x0),c(r.y0),c(r.x1),c(r.y1)};
    if(r.x1<r.x0)std::swap(r.x0,r.x1);if(r.y1<r.y0)std::swap(r.y0,r.y1);return r;
}
inline Rect Uv(const Source& s,Rect r){
    return {(s.x+r.x0*s.width)/s.textureWidth,(s.y+r.y0*s.height)/s.textureHeight,
            (s.x+r.x1*s.width)/s.textureWidth,(s.y+r.y1*s.height)/s.textureHeight};
}
struct Layout {Rect health,ammo,inventory,resources,focus,messages,front[32];unsigned count=0;bool dashboard=false,vrText=false,messagesLeft=false;};
// Subtraction preserves every source pixel exactly once, including messages
// outside the usual prompt/subtitle bands. No guessed text/color detection.
inline Layout Partition(Rect health,Rect ammo,bool dashboard=false,bool vrText=false,bool messagesLeft=false){
    Layout out{};out.health=Clamp(health);out.ammo=Clamp(ammo);out.dashboard=dashboard;out.vrText=vrText;out.messagesLeft=messagesLeft;
    // HUDPC: CurrentAbilityName/HandsIcons at lower left; native rightBox
    // AdamCount/CreditCount at y~353 of the 720px authored canvas.
    if(dashboard){out.inventory={0,.72f,.26f,1};out.resources={.86f,.30f,1,.66f};}
    if(vrText)out.focus={.27f,.23f,.80f,.71f};
    // Original HUDPC routes Quests to rightBox.chatBoxMovie.importantTextHolder
    // in the upper-right band. Extract the complete band, never text by colour.
    if(messagesLeft)out.messages={.50f,0,1,.23f};
    Rect pieces[64]={{0,0,1,1}};unsigned count=1;
    for(Rect cut:{out.health,out.ammo,out.inventory,out.resources,out.focus,out.messages}){
        if(cut.x1<=cut.x0||cut.y1<=cut.y0)continue;
        Rect next[64];unsigned n=0;
        for(unsigned i=0;i<count;i++){
            Rect a=pieces[i],b{std::max(a.x0,cut.x0),std::max(a.y0,cut.y0),std::min(a.x1,cut.x1),std::min(a.y1,cut.y1)};
            if(b.x1<=b.x0||b.y1<=b.y0){next[n++]=a;continue;}
            Rect split[]={{a.x0,a.y0,a.x1,b.y0},{a.x0,b.y1,a.x1,a.y1},
                          {a.x0,b.y0,b.x0,b.y1},{b.x1,b.y0,a.x1,b.y1}};
            for(Rect r:split)if(r.x1>r.x0&&r.y1>r.y0)next[n++]=r;
        }
        count=n;for(unsigned i=0;i<n;i++)pieces[i]=next[i];
    }
    for(unsigned i=0;i<count&&i<32;i++)out.front[out.count++]=pieces[i];return out;
}
}
