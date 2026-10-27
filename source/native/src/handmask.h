#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
#include "handmask.inc"
namespace handmask {
inline std::uint64_t Key(unsigned a,unsigned b,unsigned c){
    if(a>b)std::swap(a,b);if(b>c)std::swap(b,c);if(a>b)std::swap(a,b);
    return std::uint64_t(a)|(std::uint64_t(b)<<16)|(std::uint64_t(c)<<32);
}
inline bool Left(unsigned i){return i<handmaskdata::vertices&&(handmaskdata::left[i/64]&(1ULL<<(i%64)));}
template<class Index> bool Select(const Index* indices,unsigned count,unsigned minVertex,std::vector<Index>& output){
    output.clear();if(!indices||count!=handmaskdata::triangles*3)return false;
    std::vector<std::uint64_t> keys;keys.reserve(count/3);
    for(unsigned i=0;i<count;i+=3){
        unsigned a=indices[i]-minVertex,b=indices[i+1]-minVertex,c=indices[i+2]-minVertex;
        if(a>=handmaskdata::vertices||b>=handmaskdata::vertices||c>=handmaskdata::vertices)return false;
        keys.push_back(Key(a,b,c));
        if(Left(a)&&Left(b)&&Left(c))output.insert(output.end(),indices+i,indices+i+3);
    }
    std::sort(keys.begin(),keys.end());std::uint64_t hash=1469598103934665603ULL;
    for(auto k:keys)hash=(hash^k)*1099511628211ULL;
    if(hash!=handmaskdata::topology||output.size()!=handmaskdata::palmTriangles*3){output.clear();return false;}
    return true;
}
}
