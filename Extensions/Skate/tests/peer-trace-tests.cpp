#include "SkatePeerTrace.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
int main() {
    const auto path=(std::filesystem::temp_directory_path()/"skate-peer-trace-selfcheck.csv").string();
    skatepeertrace::Capture capture;
    skatepeertrace::Row row; row.guid=42; row.local=true; row.actor[0]=1.5f; row.run=3; row.current=3;
    assert(!capture.Write(1000,row)); // Default off means no capture/writes.
    assert(capture.Start(path,1000,1234) && capture.Active());
    assert(capture.Write(1000,row)); assert(!capture.Write(1049,row)); assert(capture.Write(1050,row));
    row.stage=4; assert(capture.Write(1050,row)); // Independently rate-limited post-transform samples.
    row.local=false; row.guid=43; assert(capture.Write(1050,row));
    assert(!capture.Write(61000,row) && !capture.Active() && capture.Rows()==4);
    assert(!capture.Start(path,62000,1234)); // One window per world entry; cannot silently restart after its limit.
    std::ifstream input(path); std::string line; unsigned count=0;
    while(std::getline(input,line)) {
        std::istringstream stream(line); std::vector<std::string> columns; std::string cell;
        while(std::getline(stream,cell,',')) columns.push_back(cell);
        assert(columns.size()==40);
        if(count) { assert(columns[1]=="1234"); assert(columns[2]==(count==4?"43":"42")); }
        ++count;
    }
    assert(count==5);
    input.close(); capture.Reset(); assert(capture.Start(path,0,5678));
    row.guid=1; row.stage=0;
    for(unsigned i=0;i<10001;++i) { row.guid=i+1; row.stage=0; capture.Write(i,row); }
    assert(capture.Rows()==64); // Key table cannot grow from arbitrary peer identities.
    capture.Stop(); capture.Stop(); assert(!capture.Active());
    capture.Reset(); assert(capture.Start(path,0,5678));
    // 16 bounded series reach the total row cap inside the time limit; no runaway logging.
    for(unsigned i=0;i<10001;++i) { row.guid=i%16+1; row.stage=0; capture.Write(i*50/16,row); }
    assert(capture.Rows()==10000 && !capture.Active());
    std::filesystem::remove(path);
}
