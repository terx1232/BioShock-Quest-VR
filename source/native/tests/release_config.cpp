#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include "../src/config.h"
#include "../src/log.h"
#include "../src/devharness.h"
void Check(bool ok,const char* m){if(!ok){fprintf(stderr,"FAIL: %s\n",m);exit(1);}}
int main(){
 char folder[MAX_PATH],ini[MAX_PATH],log[MAX_PATH];GetTempPathA(MAX_PATH,folder);
 snprintf(ini,sizeof(ini),"%sBioShock-release-%lu.ini",folder,GetCurrentProcessId());
 snprintf(log,sizeof(log),"%sBioShock-release-%lu.log",folder,GetCurrentProcessId());
 const char* enabled[]={"DevHarness","LogPoses","GpuTiming","CpuTiming","DebugDump","DepthDebug","BobLog"};
 for(auto key:enabled)WritePrivateProfileStringA("VR",key,"1",ini);
 WritePrivateProfileStringA("VR","DebugClickU","0.5",ini);
 WritePrivateProfileStringA("VR","GameResolution","864x864",ini);
 WritePrivateProfileStringA("VR","HudSupersample","2",ini);
 WritePrivateProfileStringA("VR","AimReticles","1",ini);
 LoadConfig(ini);LogInit(log);Log("This must not create a runtime log");
 Check(!g_config.devHarness&&!g_config.gpuTiming&&!g_config.cpuTiming&&!g_config.logPoses&&!g_config.debugDump&&!g_config.depthDebug&&!g_config.bobLog,"all diagnostic overrides are compiled off");
 Check(g_config.debugClickU<0&&g_config.debugClickV<0,"forced cursor probes cannot be enabled");
 Check(g_config.gameWidth==864&&g_config.gameHeight==864&&g_config.hudSupersample==2,"graphics dimensions and HUD quality retained");
 Check(g_config.aimReticles,"gameplay aim markers remain configurable in release");
 Check(GetFileAttributesA(log)==INVALID_FILE_ATTRIBUTES,"LogInit/Log never create a runtime file");
 bool buttons[24]={};buttons[3]=true;float lx=.1f,ly=.2f,rx=.3f,ry=.4f;devharness::Init(folder);devharness::Apply(buttons,lx,ly,rx,ry);
 float u=9,v=8;Check(buttons[3]&&lx==.1f&&ly==.2f&&rx==.3f&&ry==.4f&&!devharness::Point(u,v)&&u==9&&v==8&&!devharness::Button(3),"test harness cannot alter actual input or supply a probe point");
 DeleteFileA(ini);puts("PASS: release refuses diagnostic/test overrides, produces no native log, retains resolution/HUD settings and cannot force input");
}
