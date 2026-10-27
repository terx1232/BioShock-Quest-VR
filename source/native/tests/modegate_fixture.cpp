#include "modegate.h"
#include "menupresentation.h"
#include <cassert>
#include <cstdio>
int main(){
    using menupresentation::Route;
    using menupresentation::Choose;
    assert(Choose(true,true,false,true,true,false)==Route::Menu);
    assert(Choose(false,false,false,true,false,true)==Route::Loading); // detached level travel, old world size
    assert(Choose(false,false,false,false,false,true)==Route::Loading); // viewport object can disappear during travel
    assert(Choose(false,false,false,true,true,true)==Route::Loading); // reattached, queued Reset not complete
    assert(Choose(false,false,false,true,true,false)==Route::Mask); // no proof of travel, never show a world window
    assert(Choose(false,true,false,true,false,true)==Route::Mask); // pause in the square viewport stays hidden
    assert(Choose(false,false,true,true,false,true)==Route::Mask); // first returning world view stays hidden
    assert(Choose(true,false,false,true,true,true)==Route::Menu); // normal loading size finally ready
    assert(Choose(true,false,true,true,true,true)==Route::Mask); // never publish returning world as loading
    ModeGate load;
    // Observed loading sequence: a 230 ms camera burst, then no view.
    for(unsigned t=0;t<240;t+=14)assert(!load.Update(true,false,1000+t));
    for(unsigned t=240;t<2000;t+=14)assert(!load.Update(false,false,1000+t));
    for(unsigned step:{14u,33u}){
        ModeGate game;
        for(unsigned t=0;t<600;t+=step)assert(!game.Update(true,false,2000+t));
        assert(game.Update(true,false,2700));
        assert(game.Update(false,false,2710)); // tolerate one missing view
        assert(!game.Update(false,false,2720)); // stop before loading UI reconstruction
        for(unsigned t=0;t<800;t+=step)game.Update(true,false,3000+t);
        assert(game.active);
        assert(!game.Update(true,true,4000)); // pause/menu wins immediately
        assert(!game.Update(true,false,4010)); // no stale readiness after menu
    }
    ModeGate rollover;
    for(unsigned i=0;i<50;++i)rollover.Update(true,false,0xffffff00u+i*14u);
    assert(rollover.active);
    puts("PASS: loading camera burst rejected, stable player view accepted at 30/72 FPS, pause and view loss leave VR, clock rollover");
}
