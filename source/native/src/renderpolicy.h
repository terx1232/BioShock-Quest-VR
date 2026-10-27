#pragma once
namespace renderpolicy {
struct State {bool worldResources,controls;unsigned width,height;};
// Tracking validity controls input/submission, not the lifetime of world GPU
// resources. Headset sleep is not a request to switch to the menu viewport.
inline State Resolve(bool game,bool tracking,bool stereo,unsigned side,unsigned w,unsigned h,float menuAspect){
    State s{game,game&&tracking,w,h};
    if(stereo&&game&&side)s.width=s.height=side;
    else if(!game&&tracking&&menuAspect>.2f)s.height=unsigned(w/menuAspect+.5f);
    if(s.height>h)s.height=h;
    return s;
}
}
