#include "../src/posewire.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits>

void Check(bool valid, const char* description) {
    if (!valid) { fprintf(stderr,"FAIL: %s\n",description);exit(1); }
}
int main(int argc,char** argv) {
    Check(argc==2,"Java fixture argument");
    FILE* file=fopen(argv[1],"rb");Check(file!=nullptr,"fixture exists");
    unsigned char bytes[posewire::kBytes];
    Check(fread(bytes,1,sizeof(bytes),file)==sizeof(bytes),"packet length");fclose(file);
    xrapi::State pose{};
    Check(posewire::Parse(bytes,sizeof(bytes),pose),"decode Java datagram");
    Check(pose.hmdOrientation.x==.12345679f && pose.hmdOrientation.y==-.23456791f,"head quaternion is not rounded");
    Check(pose.hmdPosition.x==.012345678f && pose.hmdPosition.y==1.654321f && pose.hmdPosition.z==-.32109876f,"head position retains precision");
    Check(pose.left.stickX==.34567892f && pose.right.stickY==-.8765432f,"stick axes");
    Check(pose.left.position.x==-.45678912f && pose.right.position.x==.56789123f,"separate controller positions");
    Check(pose.left.grip.w==1 && pose.right.grip.w==1,"controller grips");
    Check(pose.sync==156 && pose.altitude==1.654321f && pose.ipd==.06345679f,"sync, floor and IPD");
    for(int i=0;i<xrapi::BUTTON_COUNT;++i)Check(pose.buttons[i]==(i==0||i==9||i==18),"button bits");
    char path[2048];snprintf(path,sizeof(path),"%s.v2",argv[1]);file=fopen(path,"rb");Check(file!=nullptr,"v2 Java fixture exists");
    unsigned char exact[posewire::kExactBytes];Check(fread(exact,1,sizeof(exact),file)==sizeof(exact),"v2 length");fclose(file);
    Check(posewire::Parse(exact,sizeof(exact),pose)&&pose.frameId==0x123456780000abcdULL,"full 64-bit producer id and epoch survive Java/Wine wire");
    Check(!posewire::Parse(exact,sizeof(exact)-1,pose),"truncated exact packet rejected");
    Check(posewire::Parse(bytes,sizeof(bytes),pose)&&pose.frameId==0,"legacy peer clears the exact id");
    Check(!posewire::Parse(bytes,sizeof(bytes)-1,pose),"truncated packet rejected");
    uint32_t invalidSync=157;memcpy(bytes+116,&invalidSync,4);
    Check(!posewire::Parse(bytes,sizeof(bytes),pose),"invalid pose index rejected");
    uint32_t validSync=156;memcpy(bytes+116,&validSync,4);
    float bad=std::numeric_limits<float>::quiet_NaN();memcpy(bytes+4+18*4,&bad,4);
    Check(!posewire::Parse(bytes,sizeof(bytes),pose),"non-finite head pose rejected");
    puts("PASS: Java-to-DLL exact head/controller poses, sync, buttons, malformed packets");
    return 0;
}
