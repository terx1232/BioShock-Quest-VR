// Preserve the actual producer pose and reference-space epoch of each image.
#include "exactpose.h"
#include "worldposeaccess.h"
#include <jni.h>
#include <dlfcn.h>
#include <android/log.h>
#include <atomic>
#include <mutex>
#include <ctime>
#include <cstdio>
#include <unistd.h>
#define XR_USE_PLATFORM_ANDROID
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#ifdef BIOSHOCK_RELEASE
#define __android_log_print(...) 0
#endif

namespace {
worldpose::Cache cache;
worldpose::Exact exact;
std::mutex lock;
std::atomic<unsigned> resume{0};unsigned seenResume=0,epoch=0;
void *renderer=nullptr,*engine=nullptr,*library=nullptr;
bool abiChecked=false,abiValid=false;
XrSession seenSession=XR_NULL_HANDLE;int seenRecenter=-1;
XrTime lastTime=0;uint64_t lastId=0;
unsigned updates=0,repeats=0,holds=0;double reportAt=0;
FILE* trace=nullptr;double traceUntil=0,traceAt=0,checkAt=0;
template<class T>T at(void* p,unsigned offset){T v;memcpy(&v,static_cast<char*>(p)+offset,sizeof(v));return v;}
double seconds(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
bool init(){
    if(!library)library=dlopen("libxr.so",RTLD_NOW|RTLD_NOLOAD);if(!library)return false;
    if(!abiChecked){
        abiChecked=true;auto fn=static_cast<unsigned char*>(dlsym(library,"Java_com_winlator_xr_XrActivity_nativeSetFramesync"));
        const unsigned expected[]={0x290e8d02,0x290f9504,0xd65f03c0};
        abiValid=fn&&!memcmp(fn+8,expected,sizeof(expected));
        if(!abiValid)__android_log_print(ANDROID_LOG_ERROR,"BioshockWorldPose","incompatible renderer ABI; stock path retained");
    }
    if(!abiValid)return false;
    if(!renderer)renderer=dlsym(library,"xr_module_renderer");if(!engine)engine=dlsym(library,"xr_module_engine");
    if(!renderer||!engine)return false;
    auto session=at<XrSession>(engine,8);auto views=at<XrView*>(renderer,0x630);
    int current=at<int>(renderer,0x1c8),recenter=at<int>(renderer,0x1d0);
    if(!session||!views||views[0].type!=XR_TYPE_VIEW||views[1].type!=XR_TYPE_VIEW||current<0||current>252||current%12)return false;
    unsigned resumed=resume.load(std::memory_order_acquire);
    if(session!=seenSession||recenter!=seenRecenter||resumed!=seenResume){
        cache.Resume();exact.Epoch(++epoch);lastTime=0;lastId=0;
        seenSession=session;seenRecenter=recenter;seenResume=resumed;
        __android_log_print(ANDROID_LOG_INFO,"BioshockWorldPose","epoch=%u session=%p recenter=%d resume=%u; old-space images rejected",epoch,(void*)session,recenter,resumed);
    }
    return true;
}
void diagnostic(unsigned tag,int status,double now){
    if(now-checkAt>=.5){
        checkAt=now;const char* marker="/data/user/0/com.winlator.cmod/files/world_pose_trace.once";
        if(access(marker,F_OK)==0){
            unlink(marker);if(trace)fclose(trace);
            trace=fopen("/data/user/0/com.winlator.cmod/files/world-pose-trace.csv","w");traceUntil=now+120;
            if(trace)fputs("monotonic,epoch,recenter,resume,tag,status,source_id,age_ms,current_sync,source_qx,source_qy,source_qz,source_qw,current_qx,current_qy,current_qz,current_qw,correction_degrees\n",trace);
        }
    }
    if(trace&&now>traceUntil){fclose(trace);trace=nullptr;}
    if(trace&&now-traceAt>=.05){
        traceAt=now;auto p=exact.Get().left;auto current=worldpose::Read(renderer,at<int>(renderer,0x1c8)).left;
        float dot=fabsf(p.qx*current.qx+p.qy*current.qy+p.qz*current.qz+p.qw*current.qw);
        float angle=2*acosf(fminf(dot,1))*180/3.14159265f;
        fprintf(trace,"%.6f,%u,%d,%u,%u,%d,%llu,%.3f,%d,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.4f\n",
            now,epoch,seenRecenter,seenResume,tag,status,(unsigned long long)exact.Id(),(now-exact.Time())*1000,
            at<int>(renderer,0x1c8),p.qx,p.qy,p.qz,p.qw,current.qx,current.qy,current.qz,current.qw,angle);fflush(trace);
    }
}
}
extern "C" JNIEXPORT void JNICALL Java_com_bioshockport_NativeCinema_resetWorldPose(JNIEnv*,jclass){
    resume.fetch_add(1,std::memory_order_release);
}
int worldpose::SelectImage(unsigned tag,worldpose::Pair& pair,unsigned* flags){
    std::lock_guard<std::mutex> guard(lock);if(tag>65535||!init())return -1;
    int status=exact.Select(tag);double now=seconds();diagnostic(tag,status,now);
    if(status>0){pair=exact.Get();if(flags)*flags=exact.Flags();}
    if(now-reportAt>=5){
        __android_log_print(ANDROID_LOG_INFO,"BioshockWorldPose","GPU image epoch=%u tag=%u source=%llu age=%.1fms status=%d",
            epoch,tag,(unsigned long long)exact.Id(),(now-exact.Time())*1000,status);reportAt=now;
    }
    return status;
}
extern "C" JNIEXPORT jlong JNICALL Java_com_bioshockport_NativeCinema_captureWorldPose(JNIEnv*,jclass,jint sync){
    std::lock_guard<std::mutex> guard(lock);if(!init()||sync!=at<int>(renderer,0x1c8))return 0;
    XrTime time=at<XrTime>(engine,0x38);
    if(time==lastTime&&lastId)return lastId;
    auto pose=worldpose::Read(renderer,sync);uint64_t id=exact.Record(pose,seconds());
    if(id){lastTime=time;lastId=id;}return static_cast<jlong>(id);
}
extern "C" JNIEXPORT void JNICALL Java_com_bioshockport_NativeCinema_registerWorldImage(JNIEnv*,jclass,jint tag,jlong id,jint flags){
    std::lock_guard<std::mutex> guard(lock);exact.Map(static_cast<unsigned>(tag),static_cast<uint64_t>(id),static_cast<unsigned>(flags));
}
extern "C" JNIEXPORT jint JNICALL Java_com_bioshockport_NativeCinema_pinExactPose(JNIEnv*,jclass,jint tag){
    if(tag<0||tag>65535)return -1;
    std::lock_guard<std::mutex> guard(lock);if(!init())return -1;
    int status=exact.Select(tag);double now=seconds();diagnostic(tag,status,now);
    if(status>0){worldpose::Write(renderer,exact.Get());if(status==2)updates++;else repeats++;}
    else holds++;
    if(now-reportAt>=5){
        __android_log_print(ANDROID_LOG_INFO,"BioshockWorldPose","exact epoch=%u image=%d source=%llu age=%.1fms status=%d fresh=%u repeat=%u rejected=%u",
            epoch,tag,(unsigned long long)exact.Id(),(now-exact.Time())*1000,status,updates,repeats,holds);
        updates=repeats=holds=0;reportAt=now;
    }
    return status>0?worldpose::kSlot:0;
}
extern "C" JNIEXPORT jint JNICALL Java_com_bioshockport_NativeCinema_pinWorldPose(JNIEnv*,jclass,jint sync,jint serial){
    if(sync<0||sync>252||sync%12||serial<0||serial>255)return -1;
    std::lock_guard<std::mutex> guard(lock);if(!init())return -1;
    int previous=cache.Token();
    if(!cache.Update(sync,serial,worldpose::Read(renderer,sync))){
        if(cache.Blocked(sync,serial)){holds++;return 0;}return -1;
    }
    worldpose::Write(renderer,cache.Get());if(previous!=cache.Token())updates++;else repeats++;
    double now=seconds();if(now-reportAt>=5){
        __android_log_print(ANDROID_LOG_INFO,"BioshockWorldPose","legacy slot=%d image=%d/%d fresh=%u repeat=%u rejected=%u; exact-frame peer not active",
            worldpose::kSlot,sync,serial,updates,repeats,holds);updates=repeats=holds=0;reportAt=now;
    }
    return worldpose::kSlot;
}
