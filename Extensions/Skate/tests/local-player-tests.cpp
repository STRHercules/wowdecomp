#include "local-player.h"
#include "skate-ground-motion.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool value){++checks;if(!value){fprintf(stderr,"FAIL local-player identity %u\n",checks);exit(1);}}
int main(){
    for(uint64_t guid:{1ull,3ull,99ull,100000ull,0xFFFFFFFFull}){
        Check(localplayer::Valid(guid));Check(localplayer::Owns(guid,guid));
        Check(!localplayer::Owns(guid+1,guid));Check(!localplayer::Owns(guid,0));
        Check(skatemotion::OwnBodyHit(guid,.5f,guid));
        Check(!skatemotion::OwnBodyHit(0,.5f,guid));
        Check(!skatemotion::OwnBodyHit(guid==1?3:1,.5f,guid));
        Check(!skatemotion::OwnBodyHit(guid,NAN,guid));Check(!skatemotion::OwnBodyHit(guid,-.1f,guid));
        Check(!skatemotion::OwnBodyHit(guid,1.1f,guid));
    }
    for(uint64_t guid:{0ull,0xF130000000000003ull,0xF110000000000001ull,0x0000000100000001ull}){
        Check(!localplayer::Valid(guid));Check(!localplayer::Owns(guid,guid));Check(!skatemotion::OwnBodyHit(guid,.5f,guid));
    }
    printf("{\"passed\":true,\"checks\":%u,\"any_local_player_guid\":true,\"foreign_bodies_not_filtered\":true,\"stale_owner_rejected\":true}\n",checks);
}
