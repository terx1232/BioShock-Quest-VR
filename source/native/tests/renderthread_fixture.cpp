#include "../../android/native/renderthread.h"
#include <cstdio>
#include <cstdlib>
void check(bool ok,const char* message){if(!ok){fprintf(stderr,"FAIL: %s\n",message);exit(1);}}
int main(){
    renderthread::Selection s;
    check(s.Tid()==0,"no stale TID before game exists");
    s.Observe(100,"Renderer\n");check(s.Tid()==100,"legacy fallback before DXVK starts");
    s.Observe(200,"dxvk-cs\n");check(s.Tid()==200,"actual DXVK command thread wins over idle Renderer");
    s.Observe(50,"Renderer\n");check(s.Tid()==200,"directory order cannot switch priority back to legacy Renderer");
    s.Observe(20,"dxvk-cs-worker\n");s.Observe(10,"dxvk-submit\n");s.Observe(-1,"dxvk-cs\n");
    check(s.Tid()==200,"no similarly named or invalid thread can take the critical role");
    renderthread::Selection reversed;reversed.Observe(200,"dxvk-cs");reversed.Observe(100,"Renderer");
    check(reversed.Tid()==s.Tid(),"reverse enumeration has identical selection");
    renderthread::Selection resumed;resumed.Observe(900,"dxvk-cs\n");
    check(resumed.Tid()==900,"new process/wake enumeration discards previous thread IDs");
    check(!strcmp(resumed.Name(),"dxvk-cs"),"diagnostics identify actual graphics thread");
    puts("PASS: real DXVK command thread selected over idle legacy Renderer; startup fallback, ordering, late startup, invalid names and replaced threads");
}
