// Wave/Output adapted from wotlkskate afa45f0 skate-audio-native.h; owned worker below.
#pragma once
#include "SkateLogic/skate-audio.h"
#include <windows.h>
#include <xaudio2.h>
#include <objbase.h>
#include <array>
#include <vector>
#include <fstream>
#include <string>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace skateaudio {
inline float GameGain(bool all, bool sfx, float master, float effects, bool focused, bool background) {
    if (!all || !sfx || (!focused && !background) || !std::isfinite(master) || !std::isfinite(effects)) return 0;
    return Clamp(master,0,1)*Clamp(effects,0,1);
}
struct Wave {
    WAVEFORMATEX format{};
    std::vector<BYTE> pcm;
    bool Decode(const std::vector<BYTE>& bytes) {
        format={}; pcm.clear();
        WAVEFORMATEX parsed{};
        std::vector<BYTE> samples;
        if (bytes.size()<44 || bytes.size()>8*1024*1024 || memcmp(bytes.data(),"RIFF",4) || memcmp(bytes.data()+8,"WAVE",4)) return false;
        uint32_t riff=0; memcpy(&riff,bytes.data()+4,4);
        if (riff!=bytes.size()-8) return false;
        bool fmt=false,data=false;
        size_t at=12;
        for (;at+8<=bytes.size();) {
            uint32_t n=0; memcpy(&n,bytes.data()+at+4,4);
            const size_t start=at+8;
            if (n>bytes.size()-start || (n&1u)>bytes.size()-start-n) return false;
            if (!memcmp(bytes.data()+at,"fmt ",4)) {
                if (fmt || n<16 || n==17) return false;
                if (n>=18) { uint16_t extra=0; memcpy(&extra,bytes.data()+start+16,2); if (extra>n-18) return false; }
                memcpy(&parsed,bytes.data()+start,16); fmt=true;
            }
            if (!memcmp(bytes.data()+at,"data",4)) {
                if (data || !n || n%2) return false;
                samples.assign(bytes.begin()+start,bytes.begin()+start+n); data=true;
            }
            at=start+n+(n&1u);
        }
        const bool valid=at==bytes.size() && fmt && data && parsed.wFormatTag==WAVE_FORMAT_PCM && parsed.nChannels==1
            && parsed.wBitsPerSample==16 && (parsed.nSamplesPerSec==48000 || parsed.nSamplesPerSec==44100)
            && parsed.nBlockAlign==2 && parsed.nAvgBytesPerSec==parsed.nSamplesPerSec*2;
        if (valid) { format=parsed; pcm=std::move(samples); }
        return valid;
    }
    bool Load(const std::wstring& path) {
        format={}; pcm.clear();
        std::ifstream f(path,std::ios::binary);
        if(!f)return false;
        f.seekg(0,std::ios::end);const auto length=f.tellg();f.seekg(0);
        if(length<44 || length>8*1024*1024)return false;
        std::vector<BYTE> bytes(static_cast<size_t>(length));
        if(!f.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))return false;
        // Owned exported bank: mono signed PCM16, 44.1kHz concrete/48kHz others.
        // XAudio2 resamples each voice at its actual rate. Reject other formats,
        // rather than interpreting compressed or malformed data as raw samples.
        return Decode(bytes);
    }
};
class Output {
    HMODULE dll_=nullptr;
    IXAudio2* engine_=nullptr;
    IXAudio2MasteringVoice* master_=nullptr;
    std::array<IXAudio2SourceVoice*,5> loops_{};
    std::array<IXAudio2SourceVoice*,12> shots_{};
    std::array<Wave,5> loopsBank_;
    std::array<std::vector<Wave>,6> sets_;
    float levels_[5]{},rates_[5]{1,1,1,1,1};
    bool playing_[5]{},gainKnown_=false;
    float gain_=0;
    uint32_t rng_=0x9e3779b9;
    bool Submit(IXAudio2SourceVoice* voice,const Wave& wave,bool loop){
        XAUDIO2_BUFFER b{};b.AudioBytes=static_cast<UINT32>(wave.pcm.size());b.pAudioData=wave.pcm.data();
        b.Flags=XAUDIO2_END_OF_STREAM;b.LoopCount=loop?XAUDIO2_LOOP_INFINITE:0;
        return SUCCEEDED(voice->SubmitSourceBuffer(&b));
    }
    bool Shot(unsigned set,float gain){
        if(sets_[set].empty())return false;
        // Never interrupt another impact/grain. A bounded voice pool avoids
        // allocating voices or allowing unbounded overlap during heavy slides.
        for(auto* voice:shots_){
            XAUDIO2_VOICE_STATE state{};voice->GetState(&state,XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if(state.BuffersQueued)continue;
            rng_^=rng_<<13;rng_^=rng_>>17;rng_^=rng_<<5;
            voice->SetVolume(gain);
            const auto& wave=sets_[set][rng_%sets_[set].size()];
            voice->Stop(); // An idle voice can remain started after its previous buffer finished.
            return SUCCEEDED(voice->SetSourceSampleRate(wave.format.nSamplesPerSec)) && Submit(voice,wave,false)
                && SUCCEEDED(voice->Start());
        }
        return false;
    }
public:
    ~Output(){
        for(auto* v:shots_)if(v)v->DestroyVoice();
        for(auto* v:loops_)if(v)v->DestroyVoice();
        if(master_)master_->DestroyVoice();if(engine_)engine_->Release();
        if(dll_)FreeLibrary(dll_);
    }
    bool Open(const std::wstring& root){
        const wchar_t* names[]={L"roll_concrete",L"roll_asphalt",L"grind_trucks",L"grind_board",L"wind"};
        const wchar_t* sets[]={L"pop",L"land",L"land_hard",L"flip",L"bail",L"skid"};
        const unsigned counts[]={4,4,6,6,6,17};
        for(unsigned i=0;i<5;++i)if(!loopsBank_[i].Load(root+L"/"+names[i]+L".wav"))return false;
        for(unsigned i=0;i<6;++i)for(unsigned n=1;n<=counts[i];++n){
            Wave w;if(!w.Load(root+L"/"+sets[i]+L"_"+std::to_wstring(n)+L".wav"))return false;
            sets_[i].push_back(std::move(w));
        }
        dll_=LoadLibraryExW(L"xaudio2_9.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!dll_)return false;
        using Create=HRESULT(WINAPI*)(IXAudio2**,UINT32,XAUDIO2_PROCESSOR);
        const auto create=reinterpret_cast<Create>(GetProcAddress(dll_,"XAudio2Create"));
        if(!create || FAILED(create(&engine_,0,XAUDIO2_DEFAULT_PROCESSOR)))return false;
        if(FAILED(engine_->CreateMasteringVoice(&master_)))return false;
        master_->SetVolume(0);
        for(unsigned i=0;i<5;++i){
            if(FAILED(engine_->CreateSourceVoice(&loops_[i],&loopsBank_[i].format)))return false;
            loops_[i]->SetVolume(0);
            if(!Submit(loops_[i],loopsBank_[i],true) || FAILED(loops_[i]->Start()))return false;
            playing_[i]=true;
        }
        for(auto& v:shots_)if(FAILED(engine_->CreateSourceVoice(&v,&sets_[0][0].format)))return false;
        return true;
    }
    unsigned Cues(const Mix& mix){
        unsigned submitted=0;
        if(mix.cues&Pop){submitted+=Shot(0,.9f);submitted+=Shot(3,.35f);}
        if(mix.cues&Land)submitted+=Shot(1,.8f);
        if(mix.cues&HardLand)submitted+=Shot(2,1);
        if(mix.cues&Bail)submitted+=Shot(4,1);
        if(mix.cues&Skid)submitted+=Shot(5,mix.skidGain);
        return submitted;
    }
    void StopShots() {
        for (auto* voice:shots_) if (voice) { voice->Stop(); voice->FlushSourceBuffers(); }
    }
    void Update(const Mix& mix,float gain,float elapsed){
        // Match Vanilla's 80ms parameter ramps and 250ms exit fade. The game
        // master/SFX mute is immediate, including already-playing one-shots.
        gain=Clamp(gain,0,1);
        if(!gainKnown_ || gain!=gain_){master_->SetVolume(gain);gain_=gain;gainKnown_=true;}
        const float amount=Clamp(elapsed/(mix.active?.08f:.25f),0,1);
        for(unsigned i=0;i<5;++i){
            const float level=levels_[i]+(mix.volume[i]-levels_[i])*amount;
            const float rate=rates_[i]+(mix.rate[i]-rates_[i])*amount;
            if(fabsf(level-levels_[i])>.00001f){levels_[i]=level;loops_[i]->SetVolume(level);}
            if(fabsf(rate-rates_[i])>.00001f){rates_[i]=rate;loops_[i]->SetFrequencyRatio(rate);}
            if(!mix.active && level<.0001f && playing_[i]){loops_[i]->Stop();playing_[i]=false;}
            else if(mix.active && !playing_[i]){loops_[i]->Start();playing_[i]=true;}
        }
    }
};

enum class Status { Disabled, Waiting, Ready, Unavailable, ComUnavailable, WorkerException };
// Tracker/queue publication is game-thread-only. The owned worker does every file/device/COM operation.
class Service {
    struct Event { Mix mix; uint32_t at,epoch; uint64_t serial; };
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    std::atomic<Status> status_{Status::Disabled};
    bool stop_=true;
    uint64_t revision_=0;
    uint64_t serial_=0;
    Sample latest_{};
    Mix mix_{};
    std::array<Event,64> queue_{};
    unsigned read_=0,count_=0;
    Tracker tracker_;
    void Work(std::wstring root) noexcept {
        try {
            const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            if (FAILED(com)) { status_=Status::ComUnavailable; return; }
            struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
            Output output;
            bool opened=false,wasActive=false;
            uint32_t previous=GetTickCount(); uint64_t revision=0;
            for (;;) {
                Sample latest; Mix mix; std::array<Event,64> pending{}; unsigned count=0; uint64_t serial;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    wake_.wait_for(lock,std::chrono::milliseconds(10),[&]{return stop_ || revision_!=revision;});
                    if (stop_) break;
                    revision=revision_; latest=latest_; mix=mix_; serial=serial_;
                    while (count_) { pending[count++]=queue_[read_]; read_=(read_+1)%queue_.size(); --count_; }
                }
                auto now=GetTickCount();
                bool active=Valid(latest) && now-latest.at<250;
                if (active && !opened) {
                    if (!output.Open(root)) { status_=Status::Unavailable; return; }
                    opened=true; status_=Status::Ready; now=GetTickCount();
                    // Loading/opening may outlast a mute, focus change or new session; never play its old snapshot.
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (stop_) break;
                    latest=latest_; mix=mix_; serial=serial_;
                    active=Valid(latest) && now-latest.at<250;
                }
                if (opened) {
                    if (!active) mix={};
                    if ((!active || latest.gain<=0) && wasActive) output.StopShots();
                    output.Update(mix,active?latest.gain:0,(std::min)((now-previous)/1000.f,.1f));
                    if (active && latest.gain>0) for (unsigned i=0;i<count;++i)
                        if (pending[i].serial==serial && pending[i].epoch==latest.epoch && now-pending[i].at<300) output.Cues(pending[i].mix);
                }
                wasActive=active && latest.gain>0; previous=now;
            }
        } catch (...) { status_=Status::WorkerException; }
    }
public:
    ~Service() { Stop(); }
    void Start(std::wstring root) {
        Stop();
        if (root.empty()) return;
        { std::lock_guard<std::mutex> lock(mutex_); stop_=false; latest_={}; mix_={}; read_=count_=0; revision_=serial_=0; tracker_.Clear(); }
        status_=Status::Waiting;
        try { worker_=std::thread(&Service::Work,this,std::move(root)); }
        catch (...) { status_=Status::WorkerException; }
    }
    void Stop() {
        { std::lock_guard<std::mutex> lock(mutex_); stop_=true; count_=0; latest_={}; }
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
        tracker_.Clear(); status_=Status::Disabled;
    }
    unsigned Publish(const Sample& sample) {
        Mix mix=tracker_.Observe(sample);
        if (sample.gain<=0) mix.cues=0; // Muted transitions are observed but never banked for later playback.
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) return 0;
        if (!Valid(sample) || sample.epoch!=latest_.epoch || sample.gain<=0) { read_=count_=0; ++serial_; }
        latest_=sample; mix_=mix; ++revision_;
        unsigned queued=0;
        if (mix.cues && count_<queue_.size()) { queue_[(read_+count_)%queue_.size()]={mix,sample.at,sample.epoch,serial_}; ++count_; queued=mix.cues; }
        wake_.notify_all();
        return queued;
    }
    Status GetStatus() const { return status_.load(); }
};
}
