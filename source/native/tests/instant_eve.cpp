#include "../src/healing.cpp"
#include <cstdio>
#include <cstdlib>
Config g_config;
void Log(const char*,...){}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){return MH_OK;}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID){return MH_OK;}
extern "C" MH_STATUS WINAPI MH_RemoveHook(LPVOID){return MH_OK;}
unsigned normalCalls=0,finishCalls=0;int inventory=3;float energy=10,capacity=100;
int __fastcall Normal(void*,void*,void*){normalCalls++;return 77;}
float __fastcall Current(void*,void*){return energy;}float __fastcall Maximum(void*,void*){return capacity;}
void __fastcall Finished(void*,void*,void*){finishCalls++;if(inventory>0){energy=capacity;inventory--;}}
void Check(bool v,const char* s){if(!v){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
int main(){
 unsigned char player[0x1000]{},item[64]{};void* table[61]{};*(void***)item=table;
 healing::original=Normal;healing::current=reinterpret_cast<healing::Scalar>(Current);healing::maximum=reinterpret_cast<healing::Scalar>(Maximum);
 healing::finish=reinterpret_cast<healing::Finish>(Finished);table[60]=(void*)healing::finish;
 g_config.instantEve=true;
 Check(healing::UseHook(item,nullptr,player)==0&&energy==100&&inventory==2&&finishCalls==1&&!normalCalls,"same native completion once, one item, no animation dispatch");
 Check(healing::UseHook(item,nullptr,player)==77&&inventory==2&&finishCalls==1&&normalCalls==1,"full EVE preserves original no-use path");
 energy=25;Check(healing::UseHook(item,nullptr,player)==0&&inventory==1&&finishCalls==2,"next real hypo uses exactly one completion, no stale pending injection");
 g_config.instantEve=false;energy=10;Check(healing::UseHook(item,nullptr,player)==77&&energy==10&&inventory==1,"disabled option preserves original behavior");
 g_config.instantEve=true;table[60]=nullptr;Check(healing::UseHook(item,nullptr,player)==77&&finishCalls==2,"overridden/unsupported item completion is never guessed");
 table[60]=(void*)healing::finish;energy=NAN;Check(healing::UseHook(item,nullptr,player)==77&&finishCalls==2,"invalid native EVE values preserve original path");
 Check(healing::UseHook(item,nullptr,nullptr)==77,"null user never enters native completion");
 puts("PASS: production instant-EVE hook, original completion once/item, native return, full/disabled/overridden/invalid/null fallbacks; no animation dispatch");
}
