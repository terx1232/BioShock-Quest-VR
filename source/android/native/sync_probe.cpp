// Debug-only ART probe. Observe the GPU image at VrGame.endFrame without
// replacing its presentation, pose, or input. Never linked into the APK.
#include <jni.h>
#include <jvmti.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <thread>
#include <chrono>
#include <vector>
#include <cstdint>
#include <dlfcn.h>

namespace {
jvmtiEnv* ti;
jmethodID endMethod,currentMethod;
jfieldID pending,r,g,b,a,cinemaField,sourceField;
jlocation location;
std::atomic<int> samples{0};
std::atomic<int> callbackCount{0};
std::atomic<unsigned> skipped{0};
std::atomic<bool> closed{false};
FILE* report;
bool captureMenu=false;
bool captureWorld=false;
bool beforeDraw=false;
jfieldID layoutField,valuesField,axesField;
void nativeState(){
    void* lib=dlopen("libxr.so",RTLD_NOW|RTLD_NOLOAD);
    void* renderer=lib?dlsym(lib,"xr_module_renderer"):nullptr;
    auto fn=lib?static_cast<unsigned char*>(dlsym(lib,"Java_com_winlator_xr_XrActivity_nativeSetFramesync")):nullptr;
    const unsigned abi[]={0x290e8d02,0x290f9504,0xd65f03c0};
    if(renderer&&fn&&!memcmp(fn+8,abi,sizeof(abi))){
        auto integer=[&](int offset){int v;memcpy(&v,static_cast<char*>(renderer)+offset,4);return v;};
        fprintf(report,"native renderer: frame_sync=%d immersive=%d sbs=%d mode=%d metadata=%d/%d/%d/%d current=%d recenter=%d\n",
            integer(0x30),integer(0x48),integer(0x40),integer(0x38),integer(0x74),integer(0x78),integer(0x7c),integer(0x80),integer(0x1c8),integer(0x1d0));fflush(report);
    }
}
void saveImage(int width,int height,const std::vector<unsigned char>& rgba){
    FILE* f=fopen("/data/user/0/com.winlator.cmod/files/menu-source-probe.bmp","wb");if(!f)return;
    unsigned char header[54]={'B','M'};
    auto put=[&](int at,uint32_t value){for(int i=0;i<4;i++)header[at+i]=(value>>(8*i))&255;};
    put(2,54+width*height*4);put(10,54);put(14,40);put(18,width);put(22,height);header[26]=1;header[28]=32;
    fwrite(header,1,sizeof(header),f);unsigned char bgra[4];
    unsigned nonblack=0;
    for(size_t p=0;p<rgba.size();p+=4){bgra[0]=rgba[p+2];bgra[1]=rgba[p+1];bgra[2]=rgba[p];bgra[3]=255;
        fwrite(bgra,1,4,f);nonblack+=rgba[p]>8||rgba[p+1]>8||rgba[p+2]>8;}
    fclose(f);fprintf(report,"source image %dx%d nonblack=%u saved menu-source-probe.bmp\n",width,height,nonblack);
}
void skip(unsigned bit,const char* why,int code=0){
    if(!(skipped.fetch_or(bit)&bit)){fprintf(report,"skip=%s code=%d\n",why,code);fflush(report);}
}
double seconds(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
jclass loaded(JNIEnv* env,const char* signature){
    jint count=0;jclass* classes=nullptr;jclass found=nullptr;
    if(ti->GetLoadedClasses(&count,&classes)!=JVMTI_ERROR_NONE)return nullptr;
    for(int i=0;i<count;i++){
        char* name=nullptr;
        if(ti->GetClassSignature(classes[i],&name,nullptr)==JVMTI_ERROR_NONE){
            if(!strcmp(name,signature))found=static_cast<jclass>(env->NewLocalRef(classes[i]));
            ti->Deallocate(reinterpret_cast<unsigned char*>(name));
        }
        env->DeleteLocalRef(classes[i]);
    }
    ti->Deallocate(reinterpret_cast<unsigned char*>(classes));return found;
}
void JNICALL atFrame(jvmtiEnv*,JNIEnv* env,jthread,jmethodID method,jlocation){
    if(closed||method!=endMethod||samples>=(captureMenu?1:8))return;
    ++callbackCount;
    if(callbackCount==1)nativeState();
    jthread thread=nullptr;ti->GetCurrentThread(&thread);
    // ART uses DEX registers; slot zero need not be the instance register.
    jobject activity=nullptr;
    jvmtiError local=ti->GetLocalInstance(thread,0,&activity);
    if(local!=JVMTI_ERROR_NONE||!activity){skip(1,"local-this",local);return;}
    if(!env->GetBooleanField(activity,pending)){skip(2,"no-pending-metadata");return;}
    int kind=env->GetIntField(activity,g);
    if(captureMenu?(captureWorld?(kind!=0&&kind!=3&&kind!=4):kind!=1):(kind!=0&&kind!=3&&kind!=4)){skip(4,"other-image-metadata",kind);return;}
    jobject cinema=env->GetObjectField(activity,cinemaField);
    jobject source=cinema?env->GetObjectField(cinema,sourceField):nullptr;
    auto image=source?static_cast<jintArray>(env->CallObjectMethod(source,currentMethod)):nullptr;
    if(env->ExceptionCheck()){env->ExceptionClear();skip(8,"source-exception");return;}
    if(!image||env->GetArrayLength(image)!=3){skip(16,"source-unavailable");return;}
    jint dimensions[3];env->GetIntArrayRegion(image,0,3,dimensions);
    if(dimensions[0]<=0||dimensions[1]<1024||dimensions[2]<512){skip(32,"source-dimensions");return;}
    GLint before,pbo,packing[4];
    const GLenum packKeys[]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_PIXELS,GL_PACK_SKIP_ROWS};
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&before);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pbo);
    for(int i=0;i<4;i++)glGetIntegerv(packKeys[i],&packing[i]);
    GLuint fbo;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_READ_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,dimensions[0],0);
    unsigned char pixel[4]={},opposite[4]={};
    bool complete=glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    double begin=seconds();
    if(complete){
        glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
        for(int i=0;i<4;i++)glPixelStorei(packKeys[i],i==0?4:0);
        glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        glReadPixels(0,dimensions[2]-1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,opposite);
        if(captureMenu){
            std::vector<unsigned char> pixels(size_t(dimensions[1])*dimensions[2]*4);
            glReadPixels(0,0,dimensions[1],dimensions[2],GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            saveImage(dimensions[1],dimensions[2],pixels);
            jobject layout=env->GetObjectField(cinema,layoutField);
            auto values=layout?static_cast<jfloatArray>(env->GetObjectField(layout,valuesField)):nullptr;
            auto axes=static_cast<jfloatArray>(env->GetObjectField(activity,axesField));
            if(values){float v[14]{};env->GetFloatArrayRegion(values,0,14,v);fprintf(report,"layout=");for(float f:v)fprintf(report," %.5f",f);fputc('\n',report);}
            if(axes&&env->GetArrayLength(axes)>=3){float v[3]{};env->GetFloatArrayRegion(axes,0,3,v);fprintf(report,"head yaw/pitch/roll= %.5f %.5f %.5f\n",v[0],v[1],v[2]);}
        }
    }
    double elapsed=(seconds()-begin)*1000;
    glBindFramebuffer(GL_READ_FRAMEBUFFER,before);glDeleteFramebuffers(1,&fbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,pbo);
    for(int i=0;i<4;i++)glPixelStorei(packKeys[i],packing[i]);
    int cpu[]={env->GetIntField(activity,r),env->GetIntField(activity,g),env->GetIntField(activity,b),env->GetIntField(activity,a)};
    int n=++samples;
    fprintf(report,"sample=%d cpu=%d,%d,%d,%d gpu0=%u,%u,%u,%u gpuLast=%u,%u,%u,%u complete=%d readMs=%.2f\n",
        n,cpu[0],cpu[1],cpu[2],cpu[3],pixel[0],pixel[1],pixel[2],pixel[3],
        opposite[0],opposite[1],opposite[2],opposite[3],complete,elapsed);
    fflush(report);
}
}
extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM* vm,char* options,void*){
    JNIEnv* env=nullptr;
    if(vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK||
       vm->GetEnv(reinterpret_cast<void**>(&ti),JVMTI_VERSION_1_2)!=JNI_OK)return JNI_ERR;
    report=fopen("/data/user/0/com.winlator.cmod/files/frame-sync-probe.log","w");
    if(!report)return JNI_ERR;
    fprintf(report,"options=%s\n",options?options:"");fflush(report);
    nativeState();
    captureMenu=options&&!strncmp(options,"capture:",8);
    captureWorld=options&&!strncmp(options,"capture:world:",14);
    beforeDraw=options&&!strncmp(options,"early:",6);
    jvmtiCapabilities capabilities{};capabilities.can_generate_breakpoint_events=1;capabilities.can_access_local_variables=1;
    jvmtiError error=ti->AddCapabilities(&capabilities);
    jclass activity=loaded(env,"Lcom/bioshockport/VrGame;");
    jclass cinema=loaded(env,"Lcom/bioshockport/NativeCinema;");
    jclass source=loaded(env,"Lcom/bioshockport/WindowSource;");
    if(error!=JVMTI_ERROR_NONE||!activity||!cinema||!source){fprintf(report,"ERROR capabilities=%d classes=%d/%d/%d\n",error,!!activity,!!cinema,!!source);fclose(report);return JNI_ERR;}
    endMethod=env->GetMethodID(activity,beforeDraw?"nativeSetFramesync":"endFrame",beforeDraw?"(IIII)V":"()V");
    currentMethod=env->GetMethodID(source,"current","()[I");
    pending=env->GetFieldID(activity,"syncPending","Z");
    r=env->GetFieldID(activity,"pendingR","I");g=env->GetFieldID(activity,"pendingG","I");
    b=env->GetFieldID(activity,"pendingB","I");a=env->GetFieldID(activity,"pendingA","I");
    cinemaField=env->GetFieldID(activity,"nativeCinema","Lcom/bioshockport/NativeCinema;");
    sourceField=env->GetFieldID(cinema,"source","Lcom/bioshockport/WindowSource;");
    if(captureMenu){
        jclass layout=loaded(env,"Lcom/bioshockport/NativeCinema$Layout;");
        if(layout){layoutField=env->GetFieldID(cinema,"layout","Lcom/bioshockport/NativeCinema$Layout;");valuesField=env->GetFieldID(layout,"values","[F");}
        axesField=env->GetFieldID(activity,"poseAxes","[F");
        if(!layout||!layoutField||!valuesField||!axesField){if(env->ExceptionCheck())env->ExceptionClear();fprintf(report,"ERROR capture fields unavailable\n");fclose(report);return JNI_ERR;}
    }
    if(env->ExceptionCheck()){env->ExceptionClear();fprintf(report,"ERROR incompatible Java fields\n");fclose(report);return JNI_ERR;}
    jlocation end;error=ti->GetMethodLocation(endMethod,&location,&end);
    if(beforeDraw){location=end;fprintf(report,"stage=before-window-draw, CPU metadata already decoded\n");fflush(report);}
    jvmtiEventCallbacks callbacks{};callbacks.Breakpoint=atFrame;
    if(error==JVMTI_ERROR_NONE)error=ti->SetEventCallbacks(&callbacks,sizeof(callbacks));
    if(error==JVMTI_ERROR_NONE)error=ti->SetBreakpoint(endMethod,location);
    if(error==JVMTI_ERROR_NONE)error=ti->SetEventNotificationMode(JVMTI_ENABLE,JVMTI_EVENT_BREAKPOINT,nullptr);
    if(error!=JVMTI_ERROR_NONE){ti->ClearBreakpoint(endMethod,location);fprintf(report,"ERROR breakpoint=%d\n",error);fclose(report);return JNI_ERR;}
    std::thread([vm]{
        for(int i=0;i<(captureMenu||beforeDraw?600:100)&&samples<(captureMenu?1:8);i++)std::this_thread::sleep_for(std::chrono::milliseconds(100));
        JNIEnv* worker=nullptr;
        if(vm->AttachCurrentThread(&worker,nullptr)!=JNI_OK){closed=true;return;}
        closed=true;ti->SetEventNotificationMode(JVMTI_DISABLE,JVMTI_EVENT_BREAKPOINT,nullptr);ti->ClearBreakpoint(endMethod,location);
        // Keep the FILE mapped and open: a callback already in progress may
        // still be finishing its last bounded readback. No future callbacks run.
        fprintf(report,"stopped samples=%d callbacks=%d; no input or presentation changes\n",samples.load(),callbackCount.load());fflush(report);
        __android_log_print(ANDROID_LOG_INFO,"BioshockFrameSync","GPU metadata probe stopped after %d samples",samples.load());
        vm->DetachCurrentThread();
    }).detach();
    return JNI_OK;
}
