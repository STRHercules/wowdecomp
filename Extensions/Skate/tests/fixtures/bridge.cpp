#include "SkateTransport.h"
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <mmsystem.h>
#pragma comment(lib,"winmm.lib")

bool Send(uint32_t op,uint32_t id,uint32_t epoch,const std::vector<unsigned char>& payload) {
    std::vector<unsigned char> frame;
    for (auto v : {skatetransport::Magic,op,id,epoch,static_cast<uint32_t>(payload.size())}) skatetransport::Word(frame,v);
    frame.insert(frame.end(),payload.begin(),payload.end());
    return fwrite(frame.data(),1,frame.size(),stdout)==frame.size() && fflush(stdout)==0;
}
int main(int argc,char** argv) {
    _setmode(_fileno(stdin),_O_BINARY); _setmode(_fileno(stdout),_O_BINARY);
    const std::string path=argc>2?argv[2]:"";
    if (path.size()<3 || (path[1]!=':' && path.substr(0,2)!="\\\\")) return 9;
    const std::string mode=path.substr(path.find_last_of("/\\")+1);
    if (mode=="delayed") timeBeginPeriod(1);
    if (mode=="hang-ready") { Sleep(INFINITE); return 0; }
    if (!Send(0,0,0,{})) return 1;
    uint64_t tick=1;
    float position=0;
    float cap=0; // op 11 ground speed cap (m/s), cleared by op 48 as in the helper; reported as velocity x
    uint32_t airborne=0; bool preview=false; // op 18 needs op 7 after every op 48, as in the helper
    // Client-owned collision session (ops 48/49/50) with the protocol doc's identity/generation checks.
    uint32_t session=0,map=0,queued=0,installed=0,failed=0;
    unsigned char origin[12]{};
    while (true) {
        unsigned char header[20];
        if (fread(header,1,20,stdin)!=20) return 0;
        auto op=skatetransport::Get(header+4), id=skatetransport::Get(header+8), epoch=skatetransport::Get(header+12), size=skatetransport::Get(header+16);
        if (size>2*1024*1024) return 2;
        std::vector<unsigned char> payload(size);
        if (fread(payload.data(),1,size,stdin)!=size) return 3;
        if (mode=="hang-request") { Sleep(INFINITE); return 0; }
        if (mode=="delayed" && (op==22 || op==20 || op==5)) Sleep(3);
        const uint32_t sent=epoch;
        if (mode=="stale") ++epoch;
        if (mode=="stale-id") ++id;
        if (mode=="oversized" || mode=="truncated") {
            uint32_t raw[] = {skatetransport::Magic,op|0x80,id,epoch,mode=="oversized"?65537u:60u};
            fwrite(raw,1,sizeof(raw),stdout); fflush(stdout); return 0;
        }
        if (mode=="bad-magic") { uint32_t raw[5]{}; fwrite(raw,1,sizeof(raw),stdout); fflush(stdout); return 0; }
        std::vector<unsigned char> out;
        if (op==48 || op==49) {
            if (size<44 || size!=44+skatetransport::Get(payload.data()+40)*36 || skatetransport::Get(payload.data()+36)) return 4;
            const uint32_t generation=skatetransport::Get(payload.data()+4);
            if (op==49) {
                if (sent!=session || skatetransport::Get(payload.data())!=map || memcmp(origin,payload.data()+8,12)
                    || generation<=queued || skatetransport::Get(payload.data()+32)) return 4;
                if (mode=="reject-refresh") {
                    const std::string why="refresh rejected";
                    if (!Send(0xff,id,epoch,{why.begin(),why.end()})) return 8;
                    continue;
                }
                queued=generation;
                if (!Send(op|0x80,id,epoch,{})) return 8;
                continue;
            }
            if (sent<=session || generation!=1 || memcmp(payload.data()+8,payload.data()+20,12)) return 4;
            session=sent; map=skatetransport::Get(payload.data()); queued=installed=1; failed=0;
            memcpy(origin,payload.data()+8,12); position=0; preview=false; cap=0;
        }
        else if (op==11) {
            if (size!=4 || sent!=session) return 4;
            cap=skatetransport::GetFloat(payload.data());
            if (!(cap>=.09144f && cap<=12.8016f)) return 4;
        }
        else if (op==50) {
            if (size || sent!=session) return 4;
            if (queued>installed) { if (mode=="fail-build") failed=queued; else installed=queued; }
            for (uint32_t v : {map,queued,installed,failed}) skatetransport::Word(out,v);
            if (!Send(op|0x80,id,epoch,out)) return 8;
            continue;
        }
        else if (op==7) {
            if (size || sent!=session) return 4;
            if (mode=="no-preview") { const std::string why="preview unavailable"; if(!Send(0xff,id,epoch,{why.begin(),why.end()})) return 8; continue; }
            // skate-visual.h schema v1: one opaque vertex, one triangle, 24 anchors.
            preview=true; for (uint32_t v : {1u,1u,3u,24u,0xff000000u,0u,0u,0u}) skatetransport::Word(out,v);
            if (!Send(op|0x80,id,epoch,out)) return 8;
            continue;
        }
        else if (op==20) {
            if (size || sent!=session) return 4;
            skatetransport::Word(out,static_cast<uint32_t>(tick)); skatetransport::Word(out,0);
            skatetransport::Word(out,mode=="bail"?300u:100u);
            if (!Send(op|0x80,id,epoch,out)) return 8;
            continue;
        }
        else if (op==9) { if(size || !preview || sent!=session) return 4; }
        else if (op==22) {
            // Op 18's 32-byte pad packet (right stick X allowed) plus one 8-byte right-stick sample per step.
            if (size!=40 || !preview || sent!=session || skatetransport::Get(payload.data())!=1
                || skatetransport::Get(payload.data()+16)!=skatetransport::Get(payload.data()+32)) return 5;
            const int32_t ry=static_cast<int32_t>(skatetransport::Get(payload.data()+20));
            if((skatetransport::Get(payload.data()+4)&0x1000) && !ry) position+=.1f; // any right-stick Y blocks push (engine: ~9000)
            airborne=static_cast<int32_t>(skatetransport::Get(payload.data()+20))>0;
            ++tick;
        }
        else if(op==5) {
            if(size!=16 || skatetransport::Get(payload.data())!=1 || sent!=session) return 5;
            if(skatetransport::Get(payload.data()+4)&0x1000) position+=.1f;
            ++tick;
        }
        else if(op==6) { if(size || sent!=session) return 6; }
        else return 7;
        skatetransport::Word(out,static_cast<uint32_t>(tick)); skatetransport::Word(out,0);
        for(float v : {position,0.f,0.f, cap,0.f,0.f, 0.f,0.f,1.f, 1.f/60.f}) skatetransport::Float(out,v);
        for(uint32_t v : {36u,airborne,1u-airborne}) skatetransport::Word(out,v);
        if (op==22 || op==9) { // preview sampling never advances the simulation tick
            for (uint32_t v : {1u,1u,24u}) skatetransport::Word(out,v);
            for (int i=0;i<25;++i) for (float v : {position,i*.05f,0.f}) skatetransport::Float(out,v);
        }
        if(!Send(op|0x80,id,epoch,out)) return 8;
    }
}
