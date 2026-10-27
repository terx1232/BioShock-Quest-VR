#pragma once
#include <cstring>

// DXVK's command-stream thread performs the time-critical graphics-device
// work. Winlator also has a thread named Renderer, which may be idle when the
// game's render_threading is disabled. Keep it as fallback until DXVK starts.
namespace renderthread {
struct Selection {
    int driver=0,fallback=0;
    void Observe(int tid,const char* name){
        if(tid<=0||!name)return;
        if(!std::strcmp(name,"dxvk-cs\n")||!std::strcmp(name,"dxvk-cs")){
            if(!driver||tid<driver)driver=tid;
        }else if(!std::strcmp(name,"Renderer\n")||!std::strcmp(name,"Renderer")){
            if(!fallback||tid<fallback)fallback=tid;
        }
    }
    int Tid()const{return driver?driver:fallback;}
    const char* Name()const{return driver?"dxvk-cs":fallback?"Renderer":"none";}
};
}
