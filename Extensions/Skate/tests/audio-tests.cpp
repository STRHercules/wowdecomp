#include "SkateAudio.h"
#include <cassert>
#include <cstdio>
#include <filesystem>

static void Word(std::vector<BYTE>& bytes,uint32_t value) {
    for (unsigned shift=0;shift<32;shift+=8) bytes.push_back(BYTE(value>>shift));
}
static std::vector<BYTE> Pcm() {
    std::vector<BYTE> b{'R','I','F','F'}; Word(b,40);
    for(char c:std::string("WAVEfmt ")) b.push_back(BYTE(c)); Word(b,16);
    for (uint32_t v : {0x00010001u,48000u,96000u,0x00100002u}) Word(b,v);
    for(char c:std::string("data")) b.push_back(BYTE(c)); Word(b,4); Word(b,0);
    return b;
}
static skateaudio::Status Wait(skateaudio::Service& service) {
    for(unsigned n=0;n<200;++n) {
        auto s=service.GetStatus();
        if (s!=skateaudio::Status::Waiting) return s;
        Sleep(10);
    }
    return service.GetStatus();
}
int main(int argc,char** argv) {
    using namespace skateaudio;
    Tracker tracker; Sample s{}; s.active=true; s.epoch=1; s.tick=1; s.at=100; s.gain=1; s.state=100; s.speed=6;
    auto mix=tracker.Observe(s);
    assert(mix.active && !mix.cues && mix.volume[0]==.9f && mix.volume[1]==0);
    s.terrain=true; ++s.tick; assert(tracker.Observe(s).volume[1]==.9f);
    s.state=201; s.vertical=2; ++s.tick; assert(tracker.Observe(s).cues==Pop);
    assert(tracker.Observe(s).cues==0);
    s.vertical=-7; ++s.tick; assert(tracker.Observe(s).cues==0);
    s.state=100; ++s.tick; assert(tracker.Observe(s).cues==HardLand);
    s.state=300; ++s.tick; assert(tracker.Observe(s).cues==Bail);
    s.state=101; s.at=1000; ++s.tick; assert(tracker.Observe(s).cues==Skid);
    s.at+=119; ++s.tick; assert(tracker.Observe(s).cues==0);
    ++s.at; ++s.tick; assert(tracker.Observe(s).cues==Skid);
    s.active=false; assert(!tracker.Observe(s).active);
    s.active=true; s.state=200; ++s.epoch; ++s.tick; assert(!tracker.Observe(s).cues);
    for (unsigned state=400;state<=405;++state) {
        s.state=state; ++s.tick; mix=tracker.Observe(s);
        const bool board=state==400 || state==402 || state==405;
        assert(mix.volume[board?3:2]>0 && mix.volume[board?2:3]==0);
    }
    assert(GameGain(true,true,.5f,.4f,true,false)==.2f);
    assert(GameGain(false,true,1,1,true,true)==0 && GameGain(true,false,1,1,true,true)==0);
    assert(GameGain(true,true,1,1,false,false)==0 && GameGain(true,true,1,1,false,true)==1);
    assert(GameGain(true,true,NAN,1,true,true)==0 && GameGain(true,true,-1,2,true,true)==0);
    Wave wave; const auto valid=Pcm(); assert(wave.Decode(valid) && wave.pcm.size()==4);
    for (size_t length : {0u,12u,36u,43u,47u}) {
        assert(!wave.Decode({valid.begin(),valid.begin()+length}) && wave.pcm.empty());
    }
    for (size_t offset : {0u,8u,12u,16u,20u,22u,24u,28u,32u,34u,36u,40u}) {
        auto bad=valid; bad[offset]^=0x7f;
        assert(!wave.Decode(bad) && wave.pcm.empty());
    }
    auto bad=valid; bad.push_back(0); assert(!wave.Decode(bad));
    bad=valid; bad.insert(bad.end(),{'J','U','N','K',1,0,0,0,0});
    uint32_t riff=uint32_t(bad.size()-8); memcpy(bad.data()+4,&riff,4);
    assert(!wave.Decode(bad)); // odd metadata chunk requires its pad byte
    bad.push_back(0); riff=uint32_t(bad.size()-8); memcpy(bad.data()+4,&riff,4); assert(wave.Decode(bad));
    assert(!wave.Load(L"missing-skate-audio-test.wav") && wave.pcm.empty());
    Service service; service.Start(L""); assert(service.GetStatus()==Status::Disabled); service.Stop();
    service.Start(L"missing-skate-audio-bank");
    s={}; s.active=true; s.epoch=1; s.tick=1; s.state=100; s.at=GetTickCount(); s.gain=0;
    assert(!service.Publish(s));
    const auto failure=Wait(service);
    assert(failure==Status::Unavailable || failure==Status::ComUnavailable);
    // The real publication path observes muted transitions without queuing/replaying them.
    s.state=201; ++s.tick; assert(!service.Publish(s));
    s.gain=1; assert(!service.Publish(s));
    s.state=100; ++s.tick; assert(service.Publish(s)==Land);
    s.epoch=2; s.state=201; ++s.tick; assert(!service.Publish(s));
    unsigned queued=0;
    for (unsigned n=0;n<200;++n) { s.state=n%2?201:100; ++s.tick; if(service.Publish(s)) ++queued; }
    assert(queued==64); // dead worker cannot grow the cue queue beyond its fixed capacity
    const auto begin=GetTickCount(); service.Stop(); service.Stop();
    assert(GetTickCount()-begin<2000 && service.GetStatus()==Status::Disabled);
    unsigned files=0; bool device=false;
    if (const char* bank=getenv("SKATE_AUDIO_BANK")) {
        const std::filesystem::path root(bank);
        for (const auto& entry:std::filesystem::directory_iterator(root)) if(entry.path().extension()==L".wav") {
            assert(wave.Load(entry.path().wstring())); ++files;
        }
        assert(files==48);
        if (argc>1 && !strcmp(argv[1],"--device-smoke")) {
            service.Start(root.wstring()); s={}; s.active=true; s.epoch=1; s.tick=1; s.at=GetTickCount(); s.state=100; s.speed=6; s.gain=0;
            service.Publish(s); const auto ready=Wait(service); device=ready==Status::Ready;
            if (device) { s.state=201; ++s.tick; service.Publish(s); Sleep(50); service.Publish({}); Sleep(30); }
            else printf("device unavailable status=%d\n",int(ready));
            service.Stop();
            if (device) {
                const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED); assert(SUCCEEDED(com));
                {
                    Output output; assert(output.Open(root.wstring()));
                    Mix silent{}; silent.active=true; silent.cues=Pop;
                    output.Update(silent,0,.01f); // Master remains zero for every submitted buffer.
                    unsigned submitted=0;
                    for (unsigned cycle=0;cycle<16;++cycle) {
                        submitted+=output.Cues(silent);
                        output.StopShots(); Sleep(20);
                    }
                    assert(submitted==32); // Reuse idle source voices beyond the 12-voice pool size.
                    printf("muted one-shot submissions=%u\n",submitted);
                }
                CoUninitialize();
            }
        }
    }
    printf("audio policy/parser/queue/lifecycle passed; real_waves=%u muted_device_ready=%d live_audibility_verified=0\n",files,device);
}
