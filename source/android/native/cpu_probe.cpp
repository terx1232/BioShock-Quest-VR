// Debug-only ART agent for comparing game CPU affinity without restarting Wine.
// Never packaged into the release APK; affects only the named BioShock process.
#include <jni.h>
#define XR_USE_PLATFORM_ANDROID
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <dlfcn.h>
#include <android/log.h>
#include <sched.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <sys/uio.h>

namespace {
const char* dir="/data/user/0/com.winlator.cmod/files/cpu-affinity";
std::string read(const std::string& path){std::ifstream f(path);return {std::istreambuf_iterator<char>(f),{}};}
std::string startTime(int pid){
    auto s=read("/proc/"+std::to_string(pid)+"/stat");auto end=s.rfind(')');
    if(end==std::string::npos)return {};
    std::istringstream in(s.substr(end+2));std::string v;
    for(int field=3;field<=22;++field)if(!(in>>v))return {};
    return v;
}
uint64_t mask(const cpu_set_t& cpus){uint64_t m=0;for(int i=0;i<64;++i)if(CPU_ISSET(i,&cpus))m|=1ULL<<i;return m;}
bool set(int tid,uint64_t m){cpu_set_t cpus;CPU_ZERO(&cpus);for(int i=0;i<64;++i)if(m&(1ULL<<i))CPU_SET(i,&cpus);return sched_setaffinity(tid,sizeof(cpus),&cpus)==0;}
struct Thread {int tid;uint64_t before;};
void inspectUi(FILE* log,int pid,uint32_t viewport){
    auto copy=[&](uint32_t address,void* data,size_t bytes){
        iovec local{data,bytes},remote{reinterpret_cast<void*>(uintptr_t(address)),bytes};
        return address>=0x10000&&process_vm_readv(pid,&local,1,&remote,1,0)==ssize_t(bytes);
    };
    auto word=[&](uint32_t address){uint32_t value=0;copy(address,&value,4);return value;};
    auto name=[&](uint32_t object){
        uint32_t index=word(object+0x24),table=word(0x112ba280),count=word(0x112ba284);
        if(!table||index>=count||count>500000)return std::string{};
        uint32_t entry=word(table+index*4);uint16_t text[96]={};
        if(!entry||!copy(entry+0x10,text,sizeof(text)))return std::string{};
        std::string result;for(unsigned i=0;i<95&&text[i];i++)result+=text[i]<128?char(text[i]):'?';return result;
    };
    auto property=[&](uint32_t object,const char* key){
        uint32_t cls=word(object+0x2c);
        for(unsigned depth=0;cls&&depth<32;depth++,cls=word(cls+0x38)){
            uint32_t field=word(cls+0x54);
            for(unsigned i=0;field&&i<4096;i++,field=word(field+0x3c))
                if(name(field)==key){int offset=int(word(field+0x6c));return offset>0&&offset<0x10000?offset:-1;}
        }
        return -1;
    };
    if(word(viewport)!=0x10fd71f4||word(viewport+0x3c)!=0x10fd71d4){fprintf(log,"ERROR viewport validation/read failed: %s\n",strerror(errno));return;}
    fprintf(log,"UI viewport=%08x size=%u/%u\n",viewport,word(viewport+0xa4),word(viewport+0xa8));
    uint32_t object=viewport;
    for(const char* key:{"InteractionMaster","FlashGUIInteraction"}){
        int offset=property(object,key);fprintf(log,"UI chain %s on %s offset=%x\n",key,name(word(object+0x2c)).c_str(),offset);
        if(offset<0)return;object=word(object+offset);if(!object)return;
    }
    for(const char* method:{"SubstituteKeyMappingTags","CallMethodString","CallMethodStringStringInt"}){
        for(uint32_t cls=word(object+0x2c),depth=0;cls&&depth<32;depth++,cls=word(cls+0x38)){
            for(uint32_t field=word(cls+0x54),i=0;field&&i<4096;i++,field=word(field+0x3c)){
                if(name(field)!=method)continue;
                fprintf(log,"UI function %s object=%08x",method,field);
                for(unsigned at=0x40;at<0xb0;at+=4)fprintf(log," +%x=%08x",at,word(field+at));
                fputc('\n',log);
            }
        }
    }
    int off=property(object,"PlayingMovies");if(off<0)return;
    uint32_t array=word(object+off),count=word(object+off+4);fprintf(log,"UI movies array=%08x count=%u\n",array,count);
    if(!array||!count||count>64)return;
    uint32_t movie=word(array+(count-1)*4);fprintf(log,"UI top=%08x name=%s type=%s\n",movie,name(movie).c_str(),name(word(movie+0x2c)).c_str());
    for(const char* key:{"MouseX","MouseY","MouseXScale","MouseYScale","ActualPosition","DefaultPosition","bShowMouseCursor","Hidden"}){
        int at=property(movie,key);if(at<0){fprintf(log,"UI property %s absent\n",key);continue;}
        uint32_t v[4]={};copy(movie+at,v,sizeof(v));float f=0;memcpy(&f,v,4);
        fprintf(log,"UI %s +%x words=%08x/%08x/%08x/%08x signed=%d float=%g\n",key,at,v[0],v[1],v[2],v[3],int(v[0]),f);
    }
}
void run(JavaVM* vm,const char* options){
    mkdir(dir,0700);
    FILE* log=fopen((std::string(dir)+"/last.log").c_str(),"w");if(!log)return;
    fprintf(log,"options=%s\ncaller=%d uid=%d\n",options?options:"",getpid(),getuid());
    char command[16]={};int pid=0;unsigned long long requested=0;
    if(!options||sscanf(options,"%15[^:]:%d:%llx",command,&pid,&requested)<2||pid<=0||pid==getpid()){
        fprintf(log,"ERROR invalid command/target\n");fclose(log);return;
    }
    std::string proc="/proc/"+std::to_string(pid);struct stat st{};
    auto cmdline=read(proc+"/cmdline");
    std::string first=cmdline.c_str();auto slash=first.rfind('/');if(slash!=std::string::npos)first=first.substr(slash+1);
    if(stat(proc.c_str(),&st)||st.st_uid!=getuid()||strcasecmp(first.c_str(),"Bioshock.exe")){
        fprintf(log,"ERROR target is not our BioShock process\n");fclose(log);return;
    }
    auto started=startTime(pid);if(started.empty()){fprintf(log,"ERROR no process start time\n");fclose(log);return;}
    fprintf(log,"cpuset=%s",read(proc+"/cpuset").c_str());
    if(!strcmp(command,"inspect-ui")){inspectUi(log,pid,uint32_t(requested));fclose(log);return;}
    if(!strcmp(command,"top-app")||!strcmp(command,"background")||!strcmp(command,"top-threads")||!strcmp(command,"bg-threads")){
        JNIEnv* env=nullptr;
        if(vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)!=JNI_OK){fprintf(log,"ERROR no JNI environment\n");fclose(log);return;}
        jclass process=env->FindClass("android/os/Process");
        bool perThread=strstr(command,"threads")!=nullptr;
        jmethodID method=process?env->GetStaticMethodID(process,perThread?"setThreadGroupAndCpuset":"setProcessGroup","(II)V"):nullptr;
        std::vector<int> targets;
        if(perThread){DIR* d=opendir((proc+"/task").c_str());if(d){while(auto* e=readdir(d)){int t=atoi(e->d_name);if(t)targets.push_back(t);}closedir(d);}}
        else targets.push_back(pid);
        if(method&&!env->ExceptionCheck())for(int tid:targets){
            env->CallStaticVoidMethod(process,method,tid,strncmp(command,"top-",4)==0?5:0);
            if(env->ExceptionCheck())break;
        }
        if(env->ExceptionCheck()){
            jthrowable error=env->ExceptionOccurred();env->ExceptionClear();
            jclass type=env->GetObjectClass(error);jmethodID text=env->GetMethodID(type,"toString","()Ljava/lang/String;");
            jstring message=text?static_cast<jstring>(env->CallObjectMethod(error,text)):nullptr;
            if(message){const char* s=env->GetStringUTFChars(message,nullptr);fprintf(log,"ERROR group: %s\n",s);env->ReleaseStringUTFChars(message,s);}
            else fprintf(log,"ERROR Android rejected process-group change\n");
            if(env->ExceptionCheck())env->ExceptionClear();
        } else if(!method)fprintf(log,"ERROR process-group API unavailable\n");
        else fprintf(log,"GROUP requested=%s actual=%s",command,read(proc+"/cpuset").c_str());
        fclose(log);return;
    }
    std::vector<Thread> threads;bool complete=true;
    DIR* tasks=opendir((proc+"/task").c_str());if(!tasks){fprintf(log,"ERROR tasks: %s\n",strerror(errno));fclose(log);return;}
    while(auto* entry=readdir(tasks)){
        int tid=atoi(entry->d_name);if(!tid)continue;cpu_set_t cpus;CPU_ZERO(&cpus);
        auto name=read(proc+"/task/"+std::to_string(tid)+"/comm");if(!name.empty()&&name.back()=='\n')name.pop_back();
        if(sched_getaffinity(tid,sizeof(cpus),&cpus)){
            if(errno!=ESRCH)complete=false;
            fprintf(log,"thread %d %s affinity ERROR %s\n",tid,name.c_str(),strerror(errno));continue;
        }
        uint64_t old=mask(cpus);threads.push_back({tid,old});
        fprintf(log,"thread %d mask=%llx %s\n",tid,(unsigned long long)old,name.c_str());
    }
    closedir(tasks);
    auto backup=std::string(dir)+"/before-"+std::to_string(pid)+".tsv";
    if(!strcmp(command,"xr-threads")||!strcmp(command,"driver-xr")||!strcmp(command,"legacy-xr")||!strcmp(command,"refresh-xr")){
        void* lib=dlopen("libxr.so",RTLD_NOW|RTLD_NOLOAD);
        void* engine=lib?dlsym(lib,"xr_module_engine"):nullptr;
        auto getProc=lib?reinterpret_cast<PFN_xrGetInstanceProcAddr>(dlsym(lib,"xrGetInstanceProcAddr")):nullptr;
        XrInstance instance{};XrSession session{};
        if(engine){memcpy(&instance,engine,sizeof(instance));memcpy(&session,static_cast<char*>(engine)+8,sizeof(session));}
        PFN_xrSetAndroidApplicationThreadKHR fn=nullptr;
        XrResult result=getProc&&instance?getProc(instance,"xrSetAndroidApplicationThreadKHR",reinterpret_cast<PFN_xrVoidFunction*>(&fn)):XR_ERROR_FUNCTION_UNSUPPORTED;
        fprintf(log,"XR thread extension query=%d session=%p\n",result,(void*)session);
        if(XR_SUCCEEDED(result)&&fn&&session){
            if(!strcmp(command,"refresh-xr")){
                int driver=0,legacy=0;
                for(auto t:threads){auto name=read(proc+"/task/"+std::to_string(t.tid)+"/comm");if(name=="dxvk-cs\n")driver=t.tid;if(name=="Renderer\n")legacy=t.tid;}
                fprintf(log,"REFRESH own-main=%d game=%d legacy=%d driver=%d\n",getpid(),pid,legacy,driver);
                result=fn(session,XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR,getpid());fprintf(log,"REFRESH alternate app result=%d\n",result);
                result=fn(session,XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR,pid);fprintf(log,"REFRESH actual app result=%d\n",result);
                if(driver){
                    if(legacy){result=fn(session,XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR,legacy);fprintf(log,"REFRESH alternate graphics result=%d\n",result);}
                    result=fn(session,XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR,driver);fprintf(log,"REFRESH actual graphics result=%d\n",result);
                }
                fprintf(log,"REFRESH policy app=%s",read(proc+"/cpuset").c_str());
                if(driver)fprintf(log,"REFRESH policy driver=%s",read(proc+"/task/"+std::to_string(driver)+"/cpuset").c_str());
                fclose(log);return;
            }
            for(auto t:threads){
                auto name=read(proc+"/task/"+std::to_string(t.tid)+"/comm");
                XrAndroidThreadTypeKHR type;
                if(!strcmp(command,"driver-xr")){
                    if(name!="dxvk-cs\n")continue;
                    type=XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR;
                }else if(!strcmp(command,"legacy-xr")){
                    if(name!="Renderer\n")continue;
                    type=XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR;
                }else if(t.tid==pid)type=XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR;
                else if(name=="Renderer\n")type=XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR;
                else if(name=="dxvk-cs\n")type=XR_ANDROID_THREAD_TYPE_RENDERER_WORKER_KHR;
                else continue;
                result=fn(session,type,t.tid);
                fprintf(log,"XR thread %d type=%d result=%d cpuset=%s",t.tid,type,result,read(proc+"/task/"+std::to_string(t.tid)+"/cpuset").c_str());
            }
        }else fprintf(log,"UNAVAILABLE: enabled XR_KHR_android_thread_settings required\n");
    } else if(!strcmp(command,"apply")){
        if(!complete||threads.empty()||!requested||(requested&~0xffULL)){
            fprintf(log,"ERROR incomplete snapshot or invalid mask\n");fclose(log);return;
        }
        std::ifstream old(backup);std::string prior;old>>prior;
        if(prior!=started){
            std::ofstream out(backup,std::ios::trunc);out<<started<<'\n';
            for(auto t:threads)out<<t.tid<<' '<<std::hex<<t.before<<std::dec<<'\n';
            out.flush();if(!out.good()){fprintf(log,"ERROR saving rollback\n");fclose(log);return;}
        }
        unsigned changed=0,errors=0;
        for(auto t:threads){if(set(t.tid,requested))++changed;else if(errno!=ESRCH){++errors;fprintf(log,"set %d ERROR %s\n",t.tid,strerror(errno));}}
        fprintf(log,"APPLIED mask=%llx threads=%u errors=%u rollback=%s\n",requested,changed,errors,backup.c_str());
    } else if(!strcmp(command,"restore")){
        std::ifstream in(backup);std::string prior;in>>prior;
        if(prior!=started){fprintf(log,"ERROR backup belongs to another process lifetime\n");fclose(log);return;}
        unsigned changed=0,errors=0;int tid;uint64_t old;
        while(in>>std::dec>>tid>>std::hex>>old){if(set(tid,old))++changed;else if(errno!=ESRCH)++errors;}
        fprintf(log,"RESTORED threads=%u errors=%u\n",changed,errors);
    } else if(strcmp(command,"inspect"))fprintf(log,"ERROR unknown command\n");
    fclose(log);
    __android_log_print(ANDROID_LOG_INFO,"BioshockPerf","CPU probe %s pid %d, threads %zu; report files/cpu-affinity/last.log",command,pid,threads.size());
}
}
extern "C" JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM* vm,char* options,void*){run(vm,options);return JNI_OK;}
