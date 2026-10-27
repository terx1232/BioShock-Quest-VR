#pragma once
#include <stdint.h>

namespace foregroundroute {
// BioShock 1.1 D3D9 mesh dispatcher: indexed list/strip/fan/quad and
// non-indexed list/strip/fan. Checked against actual EXE call instructions.
inline bool MeshSite(uintptr_t site){
    return site==0x10b917b4||site==0x10b91818||site==0x10b91888||
           site==0x10b918d3||site==0x10b917cd||site==0x10b91838;
}
enum class Action { Keep, Capture };
inline bool WrenchMesh(bool selected,unsigned vertices,unsigned primitives){
    // WP_WrenchMesh signature verified by actual Quest draw trace and the
    // existing mesh-isolation captures. Do not match arbitrary world meshes.
    return selected&&vertices==1953&&primitives==1686;
}
inline bool PistolMesh(bool selected,unsigned vertices,unsigned primitives){
    // WP_PistolMesh LODs, verified by package data and the real Quest draw trace.
    return selected&&((vertices==2361&&primitives==2824)||(vertices==1687&&primitives==1803)||
                      (vertices==889&&primitives==916)||(vertices==312&&primitives==269));
}
inline bool NavigationMesh(bool selected,unsigned vertices,unsigned primitives){
    return selected&&vertices==67&&primitives==78; // original GPS_arrow asset
}
inline Action Decide(uintptr_t site,bool worldReady,bool matchedModel){
    if(!worldReady||!matchedModel||!MeshSite(site))return Action::Keep;
    // The real Quest trace has normal frames with no 0x10b91e43 canvas
    // command at all. Its presence cannot identify a weapon pass. Capture
    // the selected mesh whenever it is drawn; DrawMesh validates its target
    // and keeps the original until that model's complete HDR chain exists.
    // Do not pre-suppress a draw before the current capture succeeds.
    return Action::Capture;
}
}
