#include "ground-grid.h"
#include <cstdio>
#include <cstdlib>
constexpr unsigned Side=25,Count=Side*Side;
struct Point { float x=0,y=0,z=0; };
struct Patch { unsigned epoch=1; Point centre{}; float height[Count]{}; bool valid[Count]{}; };
static unsigned checks=0;
static void Check(bool value) { ++checks; if (!value) { fprintf(stderr,"FAIL grid check %u\n",checks); exit(1); } }
int main() {
    Patch old{};
    for (unsigned i=0;i<Count;++i) { old.height[i]=static_cast<float>(i); old.valid[i]=i%7!=0; }
    for (int sx=-12;sx<=12;++sx) for (int sy=-12;sy<=12;++sy) {
        Patch next{}; next.centre={sx*2.f,sy*2.f,1}; bool known[Count]{};
        const unsigned count=groundgrid::Reuse<Side>(old,next,known,2);
        Check(count==static_cast<unsigned>((25-abs(sx))*(25-abs(sy))));
        for (unsigned y=0;y<Side;++y) for (unsigned x=0;x<Side;++x) {
            const int ox=static_cast<int>(x)+sx,oy=static_cast<int>(y)+sy;
            const unsigned i=y*Side+x;
            const bool inside=ox>=0 && oy>=0 && ox<25 && oy<25;
            Check(known[i]==inside);
            if (inside) {
                const unsigned j=static_cast<unsigned>(oy)*Side+static_cast<unsigned>(ox);
                Check(next.height[i]==old.height[j] && next.valid[i]==old.valid[j]);
            }
        }
    }
    Patch different{}; different.epoch=2; bool known[Count]{};
    Check(groundgrid::Reuse<Side>(old,different,known,2)==0);
    different.epoch=1; different.centre.x=.5f;
    Check(groundgrid::Reuse<Side>(old,different,known,2)==0);
    printf("{\"passed\":true,\"checks\":%u,\"shifts\":625,\"queries_after_four_yard_shift\":50}\n",checks);
}
