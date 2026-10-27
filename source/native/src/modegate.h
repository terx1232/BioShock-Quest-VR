#pragma once
#include <stdint.h>

// A temporary CalcView during a level load is not a ready player view.
struct ModeGate {
    bool active=false;
    unsigned views=0,missing=0;
    uint32_t since=0;
    bool Update(bool playerView,bool menu,uint32_t now){
        if(menu){active=false;views=missing=0;return active;}
        if(playerView){
            missing=0;
            if(views++==0)since=now;
            if(views>=12&&uint32_t(now-since)>=600)active=true;
        } else {
            views=0;
            if(++missing>=2)active=false;
        }
        return active;
    }
};
