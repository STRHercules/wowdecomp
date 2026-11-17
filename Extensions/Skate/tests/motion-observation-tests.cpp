#include "motion-observation.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool value) { ++checks; if (!value) { fprintf(stderr,"FAIL motion observation %u\n",checks); exit(1); } }
int main() {
    motionobservation::Speed state;
    Check(state.Step(1,0,0,0,14)==14);
    float value=14;
    for (unsigned now=40;now<=1000;now+=40) { value=state.Step(1,now,now*.001f*12,0,14); Check(value>=12 && value<=14); }
    Check(fabsf(value-12)<.001f);
    Check(state.Step(2,1001,0,0,7)==7);
    Check(state.Step(2,1010,0,0,7)==7);
    Check(state.Step(2,1041,100,0,7)==7); // teleports are not velocity feedback
    state.Clear(); Check(state.Step(1,0xfffffff0u,0,0,7)==7);
    Check(fabsf(state.Step(1,24,.28f,0,7)-7)<.00001f);
    Check(state.Step(1,400,100,0,7)==7); // stale interval reset
    Check(state.Step(0,410,0,0,7)==7 && !state.active);
    printf("{\"passed\":true,\"checks\":%u,\"native_travel_measured\":true,\"epoch_stale_teleport_reset\":true,\"position_writes\":false}\n",checks);
}
