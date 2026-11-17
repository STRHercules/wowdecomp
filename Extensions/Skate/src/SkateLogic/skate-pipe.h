#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include <cmath>

namespace skatewire {
constexpr uint32_t Magic=0x31425357;
inline void Word(std::vector<unsigned char>& bytes,uint32_t word) {
    const auto* raw=reinterpret_cast<const unsigned char*>(&word);
    bytes.insert(bytes.end(),raw,raw+4);
}
inline void Float(std::vector<unsigned char>& bytes,float value) {
    uint32_t bits=0; memcpy(&bits,&value,4); Word(bytes,bits);
}
inline uint32_t Get(const unsigned char* bytes) { uint32_t word=0; memcpy(&word,bytes,4); return word; }
inline float GetFloat(const unsigned char* bytes) { float value=0; memcpy(&value,bytes,4); return value; }
inline void InputStep(std::vector<unsigned char>& bytes,unsigned steps,bool push,bool brake,bool left,bool right,int strength=18000) {
    bytes.clear();
    Word(bytes,steps);
    Word(bytes,brake?0x2000u:push?0x1000u:0u);
    if (strength<0 || strength>32767) strength=18000;
    const int axis=left==right?0:left?-strength:strength;
    Word(bytes,static_cast<uint32_t>(axis));
    Word(bytes,0);
}
  inline void ControllerStep(std::vector<unsigned char>& bytes,unsigned steps,bool push,bool brake,
      bool left,bool right,uint16_t buttons,int16_t lx,int16_t ry) {
      bytes.clear(); Word(bytes,steps);
      unsigned pressed=buttons&0x7000u;
      if (push) pressed|=0x1000u;
      if (brake || (pressed&0x2000u)) pressed=0x2000u;
      Word(bytes,pressed);
      const int x=left || right?(left==right?0:left?-32767:32767):lx;
      Word(bytes,static_cast<uint32_t>(x)); Word(bytes,0);
      Word(bytes,0); Word(bytes,static_cast<uint32_t>((std::min)(0,static_cast<int>(ry))));
      Word(bytes,0); Word(bytes,0); // grabs/triggers wait for the airborne stage
  }
  inline void OllieStep(std::vector<unsigned char>& bytes,unsigned steps,bool push,bool brake,
      bool left,bool right,uint16_t buttons,int16_t lx,int16_t ry) {
      ControllerStep(bytes,steps,push,brake,left,right,buttons,lx,ry);
      const uint32_t fullRight=static_cast<uint32_t>(static_cast<int32_t>(ry));
      memcpy(bytes.data()+20,&fullRight,4); // ONLY op18 accepts the owned pop axis.
  }
  inline void TrickStep(std::vector<unsigned char>& bytes,unsigned steps,bool push,bool brake,
      bool left,bool right,uint16_t buttons,int16_t lx,int16_t rx,int16_t ry) {
      OllieStep(bytes,steps,push,brake,left,right,buttons,lx,ry);
      const uint32_t fullX=static_cast<uint32_t>(static_cast<int32_t>(rx));
      memcpy(bytes.data()+16,&fullX,4); // Only new op22/23 accepts the full right stick.
  }
  inline void GrabSample(std::vector<unsigned char>& bytes,int16_t x,int16_t y,uint8_t lt,uint8_t rt) {
      Word(bytes,static_cast<uint32_t>(static_cast<int32_t>(x)));
      Word(bytes,static_cast<uint32_t>(static_cast<int32_t>(y)));
      Word(bytes,lt); Word(bytes,rt); // New op28/29 only: timestamped raw triggers.
  }
  inline bool LeftForward(std::vector<unsigned char>& bytes,int16_t y){
      if(bytes.size()!=32)return false;
      const uint32_t value=static_cast<uint32_t>(static_cast<int32_t>(y));
      memcpy(bytes.data()+12,&value,4);return true;
  }
  struct Pose {
    uint64_t tick=0;
    float position[3]{},velocity[3]{},forward[3]{},period=0;
    uint32_t bones=0,airborne=0,ground=0;
};
inline bool Decode(const std::vector<unsigned char>& bytes,Pose& pose) {
    if (bytes.size()!=60) return false;
    memcpy(&pose.tick,bytes.data(),8);
    for (unsigned i=0;i<3;++i) {
        pose.position[i]=GetFloat(bytes.data()+8+i*4);
        pose.velocity[i]=GetFloat(bytes.data()+20+i*4);
        pose.forward[i]=GetFloat(bytes.data()+32+i*4);
        if (!std::isfinite(pose.position[i]) || fabsf(pose.position[i])>10000
            || !std::isfinite(pose.velocity[i]) || fabsf(pose.velocity[i])>100
            || !std::isfinite(pose.forward[i]) || fabsf(pose.forward[i])>1.1f) return false;
    }
    pose.period=GetFloat(bytes.data()+44);
    pose.bones=Get(bytes.data()+48); pose.airborne=Get(bytes.data()+52); pose.ground=Get(bytes.data()+56);
    return pose.tick>0 && std::isfinite(pose.period) && pose.period>0 && pose.period<=0.1f
        && pose.bones==36 && pose.airborne<=1 && pose.ground<=1;
}

// Owned child process only. The render callback never performs pipe I/O.
class Pipe {
    HANDLE input_=nullptr,output_=nullptr,process_=nullptr,job_=nullptr,watch_=nullptr,stop_=nullptr;
    volatile LONG deadline_=0;
    volatile LONG* liveEpoch_=nullptr;
    LONG epoch_=0;
    uint32_t serial_=0;
    std::string rejection_;
    bool healthy_=false,replyComplete_=false;
    static DWORD WINAPI Watch(void* context) {
        auto* self=static_cast<Pipe*>(context);
        while (WaitForSingleObject(self->stop_,10)==WAIT_TIMEOUT) {
            const LONG deadline=InterlockedCompareExchange(&self->deadline_,0,0);
            if ((self->liveEpoch_ && InterlockedCompareExchange(self->liveEpoch_,0,0)!=self->epoch_)
                || (deadline && static_cast<LONG>(GetTickCount()-static_cast<DWORD>(deadline))>=0)) {
                TerminateJobObject(self->job_,ERROR_TIMEOUT);
                return 0;
            }
        }
        return 0;
    }
    bool Read(void* destination,DWORD count) {
        auto* bytes=static_cast<unsigned char*>(destination);
        while (count) {
            DWORD available=0;
            if (!PeekNamedPipe(output_,nullptr,0,nullptr,&available,nullptr)) return false;
            if (!available) {
                if (WaitForSingleObject(process_,0)!=WAIT_TIMEOUT) return false;
                Sleep(1); continue;
            }
            DWORD received=0;
            if (!ReadFile(output_,bytes,count<available?count:available,&received,nullptr) || !received) return false;
            bytes+=received; count-=received;
        }
        return true;
    }
    bool Reply(uint32_t expectedOp,uint32_t expectedId,uint32_t expectedEpoch,std::vector<unsigned char>& bytes) {
        replyComplete_=false;
        unsigned char header[20]{};
        const unsigned maximum=expectedOp==(0x80u|7u)?2u*1024u*1024u:65536u;
        if (!Read(header,20) || Get(header)!=Magic
            || Get(header+8)!=expectedId || Get(header+12)!=expectedEpoch || Get(header+16)>maximum) return false;
        const uint32_t replyOp=Get(header+4);
        if (replyOp!=expectedOp && replyOp!=0xffu) return false;
        bytes.resize(Get(header+16));
        // A bounded, correctly addressed rejection has a payload too. Drain it
        // so the next independent request cannot read an error string as a header.
        const bool complete=bytes.empty() || Read(bytes.data(),static_cast<DWORD>(bytes.size()));
        replyComplete_=complete;
        if(complete && replyOp==0xffu){
            for(size_t i=0;i<bytes.size() && i<512;++i){const auto ch=bytes[i];rejection_.push_back(ch>=32 && ch<=126?static_cast<char>(ch):'?');}
        }
        return complete && replyOp==expectedOp;
    }
public:
    bool Running() const {return healthy_ && process_ && WaitForSingleObject(process_,0)==WAIT_TIMEOUT;}
    DWORD ProcessId() const {return process_?GetProcessId(process_):0;}
    const char* Rejection() const {return rejection_.empty()?"transport_or_decode":rejection_.c_str();}
    Pipe()=default;
    Pipe(const Pipe&)=delete;
    Pipe& operator=(const Pipe&)=delete;
    ~Pipe() { Stop(); }
    void Stop() {
        healthy_=false;
        if (stop_) SetEvent(stop_);
        if (watch_) { WaitForSingleObject(watch_,1000); CloseHandle(watch_); watch_=nullptr; }
        // The job owns only the helper started by this instance, never WoW/server processes.
        if (job_) TerminateJobObject(job_,0);
        if (process_) { WaitForSingleObject(process_,1000); CloseHandle(process_); process_=nullptr; }
        for (HANDLE* handle : {&input_,&output_,&job_,&stop_}) {
            if (*handle) CloseHandle(*handle);
            *handle=nullptr;
        }
    }
    bool Start(const wchar_t* binary,const wchar_t* assets,const wchar_t* log,LONG epoch,volatile LONG* liveEpoch,bool appendLog=false) {
        Stop(); serial_=0; epoch_=epoch; liveEpoch_=liveEpoch;
        DWORD binaryType=0;
        if (!GetBinaryTypeW(binary,&binaryType) || binaryType!=SCS_64BIT_BINARY) return false;
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
        HANDLE childInput=nullptr,childOutput=nullptr;
        if (!CreatePipe(&childInput,&input_,&security,4096)) return false;
        if (!CreatePipe(&output_,&childOutput,&security,4096)) { CloseHandle(childInput); return false; }
        SetHandleInformation(input_,HANDLE_FLAG_INHERIT,0);
        SetHandleInformation(output_,HANDLE_FLAG_INHERIT,0);
        HANDLE errors=CreateFileW(log,appendLog?FILE_APPEND_DATA:GENERIC_WRITE,FILE_SHARE_READ,&security,appendLog?OPEN_ALWAYS:CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (errors==INVALID_HANDLE_VALUE) { CloseHandle(childInput); CloseHandle(childOutput); return false; }
        job_=CreateJobObjectW(nullptr,nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
        limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        bool okay=job_ && SetInformationJobObject(job_,JobObjectExtendedLimitInformation,&limit,sizeof(limit));
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb=sizeof(startup);
        startup.StartupInfo.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
        startup.StartupInfo.wShowWindow=SW_HIDE;
        startup.StartupInfo.hStdInput=childInput; startup.StartupInfo.hStdOutput=childOutput; startup.StartupInfo.hStdError=errors;
        SIZE_T size=0;
        InitializeProcThreadAttributeList(nullptr,1,0,&size);
        startup.lpAttributeList=static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(),0,size));
        bool attributes=startup.lpAttributeList && InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&size);
        HANDLE inherited[]={childInput,childOutput,errors};
        okay=okay && attributes && UpdateProcThreadAttribute(startup.lpAttributeList,0,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr);
        std::wstring command=L"\""+std::wstring(binary)+L"\" --shadow \""+assets+L"\"";
        PROCESS_INFORMATION process{};
        if (okay) okay=CreateProcessW(binary,&command[0],nullptr,nullptr,TRUE,
            CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,
            L"D:\\world of skatecraft 3.3.5\\native-probe",&startup.StartupInfo,&process)!=FALSE;
        if (attributes) DeleteProcThreadAttributeList(startup.lpAttributeList);
        if (startup.lpAttributeList) HeapFree(GetProcessHeap(),0,startup.lpAttributeList);
        CloseHandle(childInput); CloseHandle(childOutput); CloseHandle(errors);
        if (!okay) return false;
        process_=process.hProcess;
        if (!AssignProcessToJobObject(job_,process_)) {
            TerminateProcess(process_,ERROR_ACCESS_DENIED); CloseHandle(process.hThread); return false;
        }
        stop_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        InterlockedExchange(&deadline_,static_cast<LONG>(GetTickCount()+20000));
        if (stop_) watch_=CreateThread(nullptr,0,Watch,this,0,nullptr);
        if (!watch_) { TerminateJobObject(job_,ERROR_NOT_ENOUGH_MEMORY); CloseHandle(process.hThread); return false; }
        ResumeThread(process.hThread); CloseHandle(process.hThread);
        std::vector<unsigned char> ready;
        okay=Reply(0,0,0,ready) && ready.empty();
        healthy_=okay;
        InterlockedExchange(&deadline_,0);
        return okay;
    }
    bool Request(uint32_t op,uint32_t epoch,const std::vector<unsigned char>& payload,std::vector<unsigned char>& result) {
        rejection_.clear();
        if (!process_ || payload.size()>2*1024*1024 || ++serial_==0) return false;
        healthy_=false;
        InterlockedExchange(&deadline_,static_cast<LONG>(GetTickCount()+((op==1 || op==16 || op==30)?20000:2000)));
        std::vector<unsigned char> bytes;
        for (uint32_t value : {Magic,op,serial_,epoch,static_cast<uint32_t>(payload.size())}) Word(bytes,value);
        bytes.insert(bytes.end(),payload.begin(),payload.end());
        size_t offset=0;
        while (offset<bytes.size()) {
            DWORD written=0;
            const DWORD chunk=static_cast<DWORD>((bytes.size()-offset)>512?512:bytes.size()-offset);
            if (!WriteFile(input_,bytes.data()+offset,chunk,&written,nullptr) || !written) return false;
            offset+=written;
        }
        const bool okay=Reply(0x80|op,serial_,epoch,result);
        healthy_=replyComplete_;
        InterlockedExchange(&deadline_,0);
        return okay;
    }
};
}
