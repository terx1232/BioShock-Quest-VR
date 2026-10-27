#ifdef BIOSHOCK_RELEASE
#include "devharness.h"
#include "stallsampler.h"
#include "crash.h"
namespace devharness {
void Init(const char*){}void BeforePresent(IDirect3DDevice9*){}
void Apply(bool[],float&,float&,float&,float&){}
bool Point(float&,float&){return false;}bool Button(int){return false;}
}
namespace stallsampler {void Start(){}void Stop(){}}
namespace crash {void Install(){}void WatchWrites(void*,int){}}
#endif
