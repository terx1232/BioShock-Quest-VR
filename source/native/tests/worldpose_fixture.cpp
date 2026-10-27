#include "../../android/native/worldposecache.h"
#include "../../android/native/exactpose.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <limits>
using namespace worldpose;
void Check(bool ok,const char* msg){if(!ok){fprintf(stderr,"FAIL: %s\n",msg);exit(1);}}
void Producer(std::vector<unsigned char>& bytes,int sync,Pair p){
    memcpy(bytes.data()+kHistory+sync*kStride,&p.left,kStride);
    memcpy(bytes.data()+kHistory+kRight+sync*kStride,&p.right,kStride);
}
Pair P(float yaw,float x){
    Pose l{0,sinf(yaw/2),0,cosf(yaw/2),x-.032f,1.7f,0};Pose r=l;r.x=x+.032f;return {l,r};
}
int main(){
    std::vector<unsigned char> renderer(0x4000,0xa5);Cache cache;Pair first=P(.3f,1);
    Producer(renderer,72,first);
    Check(cache.Update(72,7,Read(renderer.data(),72)),"first image accepts the actual left/right camera poses");
    Write(renderer.data(),cache.Get());
    for(int frame=0;frame<100;frame++){
        int sync=(frame%22)*12;Producer(renderer,sync,P(frame*.025f,float(frame)/100));
        // Reusing the SAME image through four full producer ring revolutions.
        Check(cache.Update(72,7,Read(renderer.data(),72)),"frozen image remains presentable");
        Write(renderer.data(),cache.Get());Pair pin=Read(renderer.data(),kSlot);
        Check(!memcmp(&pin,&first,sizeof(pin)),"recycled ring must not change the frozen image pose");
    }
    Pair next=P(-.2f,2);Producer(renderer,72,next);
    Check(cache.Update(72,8,Read(renderer.data(),72)),"new image with SAME pose index is distinguished by serial");
    Write(renderer.data(),cache.Get());Pair saved=Read(renderer.data(),kSlot);
    Check(!memcmp(&saved,&next,sizeof(saved)),"both eyes update together");
    Check(fabsf(saved.right.x-saved.left.x-.064f)<.00001f,"physical IPD preserved");
    cache.Resume();cache.Resume();
    Check(cache.Blocked(72,8),"old image identity survives repeated lifecycle reset");
    Check(!cache.Update(72,8,next),"pause+resume/recenter cannot rebind the old image to a new space");
    Check(cache.Update(84,9,P(.4f,3)),"first fresh image resumes presentation");
    Check(!cache.Update(13,10,first)&&!cache.Update(264,10,first)&&!cache.Update(-12,10,first),"invalid sync indices rejected");
    Pair invalid=first;invalid.left.qw=std::numeric_limits<float>::quiet_NaN();
    Check(!cache.Update(96,10,invalid),"nonfinite historical pose rejected");invalid=first;invalid.right.qw=0;
    Check(!cache.Update(96,10,invalid),"incomplete eye pose rejected");
    Check(!cache.Blocked(96,10),"invalid fresh data is distinguished from a held pre-resume image");
    Check(cache.Update(96,255,first)&&cache.Update(108,0,next),"serial byte rollover");
    auto before=renderer;Write(renderer.data(),cache.Get());
    for(unsigned i=0;i<renderer.size();i++){
        bool slot=(i>=kHistory+kSlot*kStride&&i<kHistory+(kSlot+1)*kStride)||
                  (i>=kHistory+kRight+kSlot*kStride&&i<kHistory+kRight+(kSlot+1)*kStride);
        if(!slot)Check(before[i]==renderer[i],"stock metadata, producer poses and renderer state remain untouched");
    }
    puts("PASS: frozen image across 100 XR frames, same-index fresh image, IPD, pause+resume/recenter gate, malformed poses, serial rollover, bounded slot writes");
    Exact exact;exact.Epoch(1);auto delayed=P(.4f,1);uint64_t id=exact.Record(delayed,1.0);
    for(int i=0;i<300;i++)exact.Record(P(i*.01f,2),1+i*.014);
    Check(exact.Map(65535,id)&&exact.Select(65535)==2,"late first presentation uses full source id after many stock-ring revolutions");
    Check(!memcmp(&exact.Get(),&delayed,sizeof(delayed)),"delayed source never uses recycled stock pose");
    for(int i=0;i<3000;i++)exact.Record(P(i*.01f,2),6+i*.014);
    Check(exact.Select(65535)==1&&!memcmp(&exact.Get(),&delayed,sizeof(delayed)),"already shown frame survives exact history eviction");
    exact.Epoch(2);Check(exact.Select(65535)==-2,"old reference-space image cannot bind a new pose after wake");
    uint64_t fresh=exact.Record(next,50);Check(exact.Select(0)==0,"UDP mapping can arrive after GPU metadata without a guessed pose");
    Check(exact.Map(0,fresh)&&exact.Select(0)==2,"new epoch and 16-bit image tag rollover resume correctly");
    for(int i=0;i<1025;i++)exact.Record(first,51+i*.014);
    exact.Map(1,fresh);Check(exact.Select(1)==-3,"expired never-presented history is rejected, not replaced with another frame");
    Check(!exact.Map(65536,fresh)&&!exact.Map(2,0),"malformed exact mapping rejected");
    Check(!exact.Record(invalid,100),"invalid producer pair cannot receive an id");
    puts("PASS: exact source ids, delayed first arrival, long held image, reference epochs, late UDP, image tag wrap, expired history");
}
