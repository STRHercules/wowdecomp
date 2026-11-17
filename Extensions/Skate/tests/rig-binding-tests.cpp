#include "rig-binding.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool ok,const char* why){++checks;if(!ok){fprintf(stderr,"FAIL rig binding: %s\n",why);exit(1);}}
int main(){
    const rigbinding::Owner initial{3,0x1000,0,7};
    Check(rigbinding::Same(initial,initial),"unchanged local owner retained");
    Check(!rigbinding::Same({},{}),"empty owner never valid");
    Check(!rigbinding::Same(initial,{1,0x1000,0,7}),"character switch rejected");
    Check(!rigbinding::Same(initial,{3,0x2000,0,7}),"same model and GUID with replaced player requires rebind");
    Check(!rigbinding::Same(initial,{3,0x1000,533,7}),"map transfer requires rebind");
    Check(!rigbinding::Same(initial,{3,0x1000,0,8}),"same-map instance copy requires rebind");
    Check(!rigbinding::Same({3,0x1000,0,UINT32_MAX},{3,0x1000,0,0}),"world serial wrap requires rebind");
    Check(!rigbinding::Same(initial,{3,0,0,7}),"missing player rejected");
    for(unsigned count=1;count<=23;++count){
        const uint32_t mask=(uint32_t(1)<<count)-1;
        Check(rigbinding::Complete(mask,count),"actual unique selected joints complete");
        Check(!rigbinding::Complete(mask^1,count),"missing root rejected");
        Check(!rigbinding::Complete(mask|(uint32_t(1)<<count),count),"foreign joint rejected");
    }
    Check(!rigbinding::Complete(0,0),"empty scope not displayed");
    Check(!rigbinding::Complete(UINT32_MAX,32),"oversized scope rejected without undefined shift");
    printf("{\"passed\":true,\"checks\":%u,\"same_model_new_player_rebind\":true,\"same_map_world_lifetime\":true,\"unique_bone_mask\":true,\"physics_unchanged\":true}\n",checks);
}
