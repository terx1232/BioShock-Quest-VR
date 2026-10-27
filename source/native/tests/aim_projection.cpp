#include <cstdio>
#include <cstdlib>
#include "../src/aimreticle.h"
void Check(bool ok,const char* text){if(!ok){fprintf(stderr,"FAIL: %s\n",text);exit(1);}}
int main(){
    vrframe::Snapshot s{};s.state.valid=true;s.frame.active=s.frame.inGame=true;
    s.frame.head=vr::YawMatrix(0);s.frame.fov=s.frame.fovV=100;
    s.state.right.orientation={0,0,0,1};s.state.left.orientation={0,.2f,0,sqrtf(.96f)};
    auto r=aimreticle::Project(s,s.state.right),l=aimreticle::Project(s,s.state.left);
    Check(r.visible&&fabsf(r.u-.5f)<.0001f&&fabsf(r.v-.5f)<.0001f,"forward weapon points at centre");
    Check(l.visible&&l.u<.4f,"left aim has its own target");
    s.state.right.position={10,20,30};s.state.right.grip={.5f,.5f,.5f,.5f};
    auto unchanged=aimreticle::Project(s,s.state.right);
    Check(unchanged.u==r.u&&unchanged.v==r.v,"grip/hand translation cannot shift native aim");
    s.frame.head=vr::YawMatrix(.25f);auto turned=aimreticle::Project(s,s.state.right);
    Check(turned.visible&&turned.u<r.u,"head rotation reprojects controller aim into current view");
    s.state.right.orientation={0,1,0,0};Check(!aimreticle::Project(s,s.state.right).visible,"backward aim hidden");
    s.state.right.orientation={0,0,0,0};Check(!aimreticle::Project(s,s.state.right).visible,"missing aim pose hidden");
    s.state.right.orientation={NAN,0,0,1};Check(!aimreticle::Project(s,s.state.right).visible,"invalid quaternion hidden");
    s.state.right.orientation={0,0,0,1};s.frame.inGame=false;
    Check(!aimreticle::Project(s,s.state.right).visible,"menus/loading have no gameplay target");
    s.frame.inGame=true;s.frame.active=false;Check(!aimreticle::Project(s,s.state.right).visible,"sleep hides target");
    s.frame.active=true;s.state.valid=false;Check(!aimreticle::Project(s,s.state.right).visible,"lost tracking hides target");
    puts("PASS: native aim orientation, independent hands, view reprojection, no grip parallax, invalid/tracking/menu guards");
}
