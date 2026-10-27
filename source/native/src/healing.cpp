#include "healing.h"
#include "config.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <cmath>
#include <cstring>

namespace healing {
namespace {
// UBioAmmoHypoBase only. Its native Use checks current/max EVE, dispatches
// BeginInjectingEveHypo, then returns zero: FinishedUsing owns consumption.
constexpr uintptr_t useAddress=0x10c7b080,finishAddress=0x10c7ae10;
using Use=int(__fastcall*)(void*,void*,void*);
using Scalar=float(__thiscall*)(void*);
using Finish=void(__thiscall*)(void*,void*);
Use original=nullptr;
Scalar current=reinterpret_cast<Scalar>(0x10c07120),maximum=reinterpret_cast<Scalar>(0x10c07110);
Finish finish=reinterpret_cast<Finish>(finishAddress);
int __fastcall UseHook(void* item,void* edx,void* user){
    if(g_config.enabled&&g_config.instantEve&&item&&user&&!IsBadReadPtr(user,0xabc)&&!IsBadReadPtr(item,4)){
        void** table=*(void***)item;
        if(table&&!IsBadReadPtr(table,61*4)&&table[60]==reinterpret_cast<void*>(finish)){
            const float value=current(user),limit=maximum(user);
            if(std::isfinite(value)&&std::isfinite(limit)&&value>=0&&limit>0&&value<limit){
                // Use is reached through the game's InventoryManager checks.
                // This same native completion refills/clamps EVE, triggers its
                // normal effect/stat notifications and removes ONE item. Do
                // not synthesize energy or run the animation callback again.
                finish(item,user);
                Log("Instant EVE: native FinishedUsing once, %.1f -> %.1f; hand injection skipped",value,current(user));
                return 0; // preserve native asynchronous-Use return convention
            }
        }
    }
    return original(item,edx,user);
}
}
bool Install(){
    if(!g_config.instantEve)return true;
    struct Check {uintptr_t address;const char* bytes;unsigned size;};
    const Check checks[]={
        {useAddress,"\x6a\xff\x68\xfe\x6e\xf6\x10\x64\xa1\x00\x00\x00\x00",13},
        {finishAddress,"\x64\xa1\x00\x00\x00\x00\x6a\xff\x68\xbe\x6e\xf6\x10",13},
        {0x10c07110,"\xd9\x81\xb8\x0a\x00\x00\xc3",7},
        {0x10c07120,"\xd9\x81\xb4\x0a\x00\x00\xc3",7}};
    for(auto c:checks){char bytes[16];SIZE_T got=0;
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(c.address),bytes,c.size,&got)||got!=c.size||memcmp(bytes,c.bytes,c.size)){
            Log("Instant EVE: native signature mismatch at %08x; original injection retained",unsigned(c.address));return false;
        }
    }
    if(MH_CreateHook(reinterpret_cast<void*>(useAddress),reinterpret_cast<void*>(UseHook),reinterpret_cast<void**>(&original))!=MH_OK)return false;
    if(MH_EnableHook(reinterpret_cast<void*>(useAddress))!=MH_OK){MH_RemoveHook(reinterpret_cast<void*>(useAddress));original=nullptr;return false;}
    Log("Instant EVE: native hypo Use hook installed; original inventory completion, no hand state/animation");return true;
}
}
