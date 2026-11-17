#include "rough-contact.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
static unsigned checks=0;
static void Check(bool ok) { ++checks; if (!ok) { fprintf(stderr,"FAIL rough contact %u\n",checks); exit(1); } }
int main() {
    roughcontact::Grace grace;
    for (unsigned state:{200u,201u,202u}) {
        grace.Clear();
        for (unsigned age=0;age<roughcontact::MaximumMs;++age)
            Check(grace.Step(1,1000+age,false,state,true,.3f,1.f));
        Check(!grace.Step(1,1400,false,state,true,.3f,1.f));
        Check(!grace.Step(1,1500,false,state,true,.3f,1.f));
        Check(!grace.Step(1,1600,true,100,true,0,0) && !grace.active);
        Check(grace.Step(1,1700,false,state,true,.1f,1));
    }
    for (unsigned state:{0u,100u,103u,300u,400u,500u,700u,999u}) {
        grace.Clear(); Check(!grace.Step(1,1000,false,state,true,.1f,0));
    }
    grace.Clear(); Check(!grace.Step(0,1000,false,200,true,.1f,0));
    Check(!grace.Step(1,1000,false,200,false,.1f,0));
    for (float gap:{-.66f,.66f,NAN,INFINITY}) Check(!grace.Step(1,1000,false,200,true,gap,0));
    for (float velocity:{-4.1f,4.1f,NAN,INFINITY}) Check(!grace.Step(1,1000,false,200,true,.1f,velocity));
    grace.Clear(); Check(grace.Step(1,0xfffffff0u,false,200,true,.1f,0));
    Check(grace.Step(1,8,false,200,true,.1f,0));
    Check(!grace.Step(1,384,false,200,true,.1f,0));
    grace.Clear(); Check(grace.Step(1,1000,false,200,true,.19f,1.f));
    Check(grace.Step(1,1266,false,200,true,.12247f,-3.37776f));
    Check(!grace.Step(1,1400,false,200,true,.12247f,-3.37776f));
    printf("{\"passed\":true,\"checks\":%u,\"supported_hops_ms\":400,\"recorded_266ms_hop_tested\":true,\"wipeout_rejected\":true,\"missing_support_rejected\":true,\"persistent_air_rejected\":true}\n",checks);
}
