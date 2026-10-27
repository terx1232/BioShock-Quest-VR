#pragma once
// Quality is initialized from the current render capacity, not a previous
// menu's smaller buffer. Unknown GPU load must not pin that old resolution.
struct DynamicResolution {
    unsigned side=0,capacity=0;
    unsigned Configure(unsigned maximum,bool enabled){
        if(maximum!=capacity||!side||!enabled)side=maximum;
        capacity=maximum;return side;
    }
    unsigned Adjust(float fps,int busy,unsigned minimum,float target){
        if(!capacity)return side;
        if(minimum>capacity)minimum=capacity;
        if(side<minimum)side=minimum;
        if(busy>88&&fps<target*.94f)side=side>minimum+64?side-64:minimum;
        else if(busy>=0&&busy<75)side=side+32<capacity?side+32:capacity;
        return side;
    }
};
