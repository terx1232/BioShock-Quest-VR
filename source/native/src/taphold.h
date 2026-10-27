#pragma once
#include <cstdint>
namespace questinput {
struct TapHold {
    bool down=false,held=false;uint32_t since=0;
    struct Action {bool tap=false,hold=false;};
    Action Update(bool pressed,uint32_t now){
        if(pressed&&!down){since=now;held=false;}
        // Include release in the duration check: a slow frame may contain
        // the release without ever observing a held sample after 450ms.
        if(down&&uint32_t(now-since)>=450)held=true;
        Action result{!pressed&&down&&!held,pressed&&held};down=pressed;return result;
    }
};
}
