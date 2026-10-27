#include <cstdio>
#include <cstdlib>
#include "../src/taphold.h"
void Check(bool v,const char* why){if(!v){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
int main(){
 questinput::TapHold input;
 auto a=input.Update(true,100);Check(!a.tap&&!a.hold,"initial press does not consume EVE early");
 a=input.Update(false,300);Check(a.tap&&!a.hold,"short release consumes one EVE");
 a=input.Update(false,350);Check(!a.tap&&!a.hold,"tap is one frame");
 input.Update(true,400);a=input.Update(true,900);Check(!a.tap&&a.hold,"hold starts hack without EVE");
 a=input.Update(false,950);Check(!a.tap&&!a.hold,"releasing hack cannot consume EVE");
 input.Update(true,1000);a=input.Update(false,1600);Check(!a.tap&&!a.hold,"slow-frame release cannot misclassify a hold as EVE tap");
 input.Update(true,0xfffffff0u);a=input.Update(false,100);Check(a.tap,"timer wrap keeps short tap");
 input={};a=input.Update(false,150);Check(!a.tap&&!a.hold,"reset during menu/sleep cancels pending consumption");
 puts("PASS: healing tap/hold, single consumption pulse, hack release, slow frame, timer wrap, menu/sleep reset");
}
