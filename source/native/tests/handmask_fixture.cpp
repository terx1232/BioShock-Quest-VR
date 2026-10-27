#include <cstdio>
#include <cstdlib>
#include "../src/handmask.h"
void Check(bool v,const char* m){if(!v){fprintf(stderr,"FAIL: %s\n",m);exit(1);}}
int main(int argc,char** argv){
 Check(argc==2,"original hand indices supplied");FILE* f=fopen(argv[1],"rb");Check(f!=nullptr,"hand fixture open");
 std::vector<unsigned short> original(handmaskdata::triangles*3),selected;
 Check(fread(original.data(),2,original.size(),f)==original.size(),"complete original topology");fclose(f);
 Check(handmask::Select(original.data(),unsigned(original.size()),0,selected)&&selected.size()==1228*3,"original hand keeps only left palm/fingers");
 std::reverse(original.begin(),original.end());Check(handmask::Select(original.data(),unsigned(original.size()),0,selected),"native winding and triangle order do not affect verification");
 std::vector<unsigned> large(original.begin(),original.end()),out;for(auto& x:large)x+=37;
 Check(handmask::Select(large.data(),unsigned(large.size()),37,out)&&out.size()==selected.size(),"32-bit indices and native min-vertex offset retained");
 original[0]=(original[0]+1)%2180;Check(!handmask::Select(original.data(),unsigned(original.size()),0,selected)&&selected.empty(),"unknown topology rejected without showing arms");
 Check(!handmask::Select(original.data(),unsigned(original.size()-3),0,selected),"partial or unrelated mesh cannot be accepted");
 puts("PASS: actual skin-derived palm mask, original native topology, winding/order/32-bit indices, offset preservation and unknown-mesh rejection");
}
