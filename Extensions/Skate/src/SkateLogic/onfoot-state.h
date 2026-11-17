#pragma once
namespace onfootstate {
inline bool Active(unsigned state){return state>=500 && state<=503;}
inline bool Ground(unsigned state){return state==500 || state==502;}
inline bool Air(unsigned state){return state==501 || state==503;}
}
