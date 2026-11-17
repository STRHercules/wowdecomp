#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace skatepeertrace {
struct Row {
    uint64_t guid=0;
    bool local=false;
    unsigned stage=0;
    float actor[3]{},movement[3]{},run=0,current=0,model[3]{},basis[9]{};
    uint32_t flags=0,epoch=0,age=0,sequence=0;
    float offset[3]{},worldOffset[3]{},boardSpeed=0,lease=0;
    uint32_t messages=0,opcode=0,messageAt=0;
};
class Capture {
    FILE* file_=nullptr;
    bool used_=false;
    uint32_t start_=0,pid_=0;
    unsigned rows_=0;
    struct Key { uint64_t guid=0; unsigned stage=0; uint32_t at=0; bool used=false; };
    std::array<Key,64> keys_{};
public:
    static constexpr uint32_t DurationMs=60000;
    static constexpr unsigned MaxRows=10000;
    ~Capture() { Stop(); }
    void Stop() { if(file_) { fflush(file_); fclose(file_); file_=nullptr; } }
    void Reset() { Stop(); used_=false; rows_=0; keys_={}; }
    bool Start(const std::string& path,uint32_t now,uint32_t pid) {
        if(used_) return false;
        used_=true; start_=now; pid_=pid;
        if(fopen_s(&file_,path.c_str(),"w") || !file_) return false;
        setvbuf(file_,nullptr,_IOFBF,64*1024);
        fprintf(file_,"ms,pid,guid,local,stage,actor_x,actor_y,actor_z,move_x,move_y,move_z,run,current,flags,model_x,model_y,model_z,b0,b1,b2,b4,b5,b6,b8,b9,b10,peer_epoch,peer_seq,peer_age_ms,board_off_x,board_off_y,board_off_z,pose_off_x,pose_off_y,pose_off_z,board_speed,lease,messages,last_opcode,last_message_ms\n");
        return true;
    }
    bool Active() const { return file_!=nullptr; }
    bool Used() const { return used_; }
    unsigned Rows() const { return rows_; }
    bool Write(uint32_t now,const Row& row) {
        if(!file_) return false;
        if(now-start_>=DurationMs || rows_>=MaxRows) { Stop(); return false; }
        Key* slot=nullptr;
        for(auto& key:keys_) {
            if(key.used && key.guid==row.guid && key.stage==row.stage) { slot=&key; break; }
            if(!key.used && !slot) slot=&key;
        }
        if(!slot || (slot->used && now-slot->at<50)) return false;
        *slot={row.guid,row.stage,now,true};
        fprintf(file_,"%u,%u,%llu,%u,%u",now,pid_,static_cast<unsigned long long>(row.guid),row.local?1u:0u,row.stage);
        for(float v:row.actor) fprintf(file_,",%.6f",v);
        for(float v:row.movement) fprintf(file_,",%.6f",v);
        fprintf(file_,",%.6f,%.6f,%u",row.run,row.current,row.flags);
        for(float v:row.model) fprintf(file_,",%.6f",v);
        for(float v:row.basis) fprintf(file_,",%.6f",v);
        fprintf(file_,",%u,%u,%u",row.epoch,row.sequence,row.age);
        for(float v:row.offset) fprintf(file_,",%.6f",v);
        for(float v:row.worldOffset) fprintf(file_,",%.6f",v);
        fprintf(file_,",%.6f,%.6f,%u,%u,%u\n",row.boardSpeed,row.lease,row.messages,row.opcode,row.messageAt);
        ++rows_;
        return true;
    }
};
}
