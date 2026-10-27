// Register the real emulated game/renderer with the already running OpenXR
// session. There is no rendering, input injection or cgroup policy change here.
#include <jni.h>
#define XR_USE_PLATFORM_ANDROID
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <android/log.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include "renderthread.h"

#ifdef BIOSHOCK_RELEASE
#define __android_log_print(...) 0
#endif

namespace {
int gamePid=0,lastPid=0,lastRenderer=0;
XrSession lastSession=XR_NULL_HANDLE;
double registeredAt=0;
bool unavailableReported=false;
void* xrLibrary=nullptr;
double reportAt=0;

bool immersive(int tid){
    char path[96],group[64]={};snprintf(path,sizeof(path),"/proc/%d/cpuset",tid);
    FILE* f=fopen(path,"r");if(!f)return false;bool ok=fgets(group,sizeof(group),f)!=nullptr;fclose(f);
    return ok&&!strncmp(group,"/immersive",10);
}

double seconds(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
bool isGame(int pid){
    char path[64],name[256]={};snprintf(path,sizeof(path),"/proc/%d",pid);
    struct stat info{};if(stat(path,&info)||info.st_uid!=getuid())return false;
    snprintf(path,sizeof(path),"/proc/%d/cmdline",pid);FILE* f=fopen(path,"rb");if(!f)return false;
    size_t n=fread(name,1,sizeof(name)-1,f);fclose(f);if(!n)return false;
    const char* base=strrchr(name,'/');return !strcasecmp(base?base+1:name,"Bioshock.exe");
}
int findGame(){
    if(gamePid&&isGame(gamePid))return gamePid;
    gamePid=0;DIR* d=opendir("/proc");if(!d)return 0;
    while(auto* e=readdir(d)){int pid=atoi(e->d_name);if(pid>0&&isGame(pid)){gamePid=pid;break;}}
    closedir(d);return gamePid;
}
renderthread::Selection rendererThreads(int pid){
    renderthread::Selection selected;
    char path[96],name[64];snprintf(path,sizeof(path),"/proc/%d/task",pid);
    DIR* d=opendir(path);if(!d)return selected;
    while(auto* e=readdir(d)){
        int tid=atoi(e->d_name);if(!tid)continue;
        snprintf(path,sizeof(path),"/proc/%d/task/%d/comm",pid,tid);FILE* f=fopen(path,"r");if(!f)continue;
        bool read=fgets(name,sizeof(name),f)!=nullptr;fclose(f);
        if(read)selected.Observe(tid,name);
    }
    closedir(d);return selected;
}
}

extern "C" JNIEXPORT jint JNICALL Java_com_bioshockport_GamePerformance_tune(JNIEnv*,jclass,jboolean force){
    if(!xrLibrary)xrLibrary=dlopen("libxr.so",RTLD_NOW|RTLD_NOLOAD);
    if(!xrLibrary)return 0;
    void* engine=dlsym(xrLibrary,"xr_module_engine");
    auto getProc=reinterpret_cast<PFN_xrGetInstanceProcAddr>(dlsym(xrLibrary,"xrGetInstanceProcAddr"));
    if(!engine||!getProc)return 0;
    XrInstance instance{};XrSession session{};
    memcpy(&instance,engine,sizeof(instance));memcpy(&session,static_cast<char*>(engine)+8,sizeof(session));
    if(!instance||!session)return 0;
    int pid=findGame();if(!pid)return 0;
    const auto selected=rendererThreads(pid);int render=selected.Tid();double now=seconds();
    // An accepted request can be followed by a system policy reset on wake.
    // Do not leave an actually-background game cached as "boosted" for 30s.
    if(!force&&pid==lastPid&&render==lastRenderer&&session==lastSession&&
       now-registeredAt<30&&immersive(pid)&&(!render||immersive(render)))return 0;
    PFN_xrSetAndroidApplicationThreadKHR fn=nullptr;
    XrResult query=getProc(instance,"xrSetAndroidApplicationThreadKHR",reinterpret_cast<PFN_xrVoidFunction*>(&fn));
    if(XR_FAILED(query)||!fn){
        if(!unavailableReported){__android_log_print(ANDROID_LOG_WARN,"BioshockPerf","XR_KHR_android_thread_settings unavailable (%d)",query);unavailableReported=true;}
        return 0;
    }
    XrResult mainResult=fn(session,XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR,pid);
    XrResult renderResult=render?fn(session,XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR,render):XR_SUCCESS;
    if(force||pid!=lastPid||now-reportAt>=5){
        __android_log_print(ANDROID_LOG_INFO,"BioshockPerf","game pid=%d main=%d graphics=%s tid=%d result=%d actual immersive=%d/%d; accepted is not policy proof",
            pid,mainResult,selected.Name(),render,renderResult,immersive(pid),render?immersive(render):0);reportAt=now;
    }
    // A worker classification is rejected by this runtime. Only the two
    // critical thread types verified on Quest are requested.
    if(XR_SUCCEEDED(mainResult)&&XR_SUCCEEDED(renderResult)){
        lastPid=pid;lastRenderer=render;lastSession=session;registeredAt=now;
    }
    return XR_SUCCEEDED(mainResult)+static_cast<int>(render&&XR_SUCCEEDED(renderResult));
}
