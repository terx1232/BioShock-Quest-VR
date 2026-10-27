#pragma once
#include "worldposecache.h"
#include <array>
#include <cstdint>
namespace worldpose {
// Capture at production time, before the stock 22-entry ring can recycle.
// No pose is inferred from the byte-sized image marker at presentation time.
class Exact {
    struct Sample {uint64_t id=0;Pair pose{};double time=0;unsigned flags=0;};
    struct Image {unsigned tag=0;uint64_t id=0;unsigned flags=0;};
    std::array<Sample,1024> history{};
    std::array<Image,1024> images{};
    uint32_t epoch=0,sequence=0;
    Sample held{};unsigned heldTag=0;bool valid=false;
public:
    void Epoch(uint32_t e){epoch=e;valid=false;}
    uint64_t Record(Pair pose,double time){
        if(!epoch||!Valid(pose.left)||!Valid(pose.right))return 0;
        uint64_t id=(uint64_t(epoch)<<32)|++sequence;
        history[sequence%history.size()]={id,pose,time};return id;
    }
    bool Map(unsigned tag,uint64_t id,unsigned flags=0){
        if(tag>65535||!id)return false;images[tag%images.size()]={tag,id,flags};return true;
    }
    // 2=new, 1=repeat, 0=packet pending, -2=old reference space, -3=expired.
    int Select(unsigned tag){
        if(valid&&heldTag==tag)return 1;
        const auto& image=images[tag%images.size()];
        if(image.tag!=tag||!image.id)return 0;
        if(uint32_t(image.id>>32)!=epoch)return -2;
        const auto& sample=history[uint32_t(image.id)%history.size()];
        if(sample.id!=image.id)return -3;
        held=sample;held.flags=image.flags;heldTag=tag;valid=true;return 2;
    }
    const Pair& Get()const{return held.pose;}
    uint64_t Id()const{return held.id;}
    double Time()const{return held.time;}
    unsigned Flags()const{return held.flags;}
    uint32_t Epoch()const{return epoch;}
};
}
