#include "ASECM3/Scene.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
std::string Quote(const std::string& s){std::string out="\"";for(unsigned char c:s){if(c=='\\'||c=='\"')out+='\\';if(c<32)out+=' ';else out+=c;}return out+'\"';}
void Point(const ASECM3::Vec3& p){std::cout<<'['<<p.x<<','<<p.y<<','<<p.z<<']';}
int main(int argc,char** argv){
 const bool summary=argc>1&&std::string(argv[argc-1])=="--summary";if(summary)--argc;
 if(argc!=4&&argc!=5){std::cerr<<"Usage: ASECM3SceneInspect <file> <source-sequence|-1> <elapsed-ms> [bank.m3a]\n";return 2;}
 try{
  const auto size=std::filesystem::file_size(argv[1]);if(size>512ull*1024*1024)throw ASECM3::Error("Scene input limit");
  std::ifstream input(argv[1],std::ios::binary);std::vector<uint8_t> bytes(static_cast<size_t>(size));if(!input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(size)))throw ASECM3::Error("Short scene read");
  auto scene=ASECM3::Scene::Parse(std::move(bytes));const auto seq=std::stoi(argv[2]);const auto elapsed=std::stoull(argv[3]);
  if(argc==5){const auto n=std::filesystem::file_size(argv[4]);if(n>128ull*1024*1024)throw ASECM3::Error("Bank input limit");std::ifstream file(argv[4],std::ios::binary);std::vector<uint8_t> bank(static_cast<size_t>(n));if(!file.read(reinterpret_cast<char*>(bank.data()),static_cast<std::streamsize>(n)))throw ASECM3::Error("Short bank read");scene=scene->WithAnimationBank(std::move(bank),argv[4],summary);}
  if(seq<-1)throw ASECM3::Error("Only -1 denotes the default source pose");
  const auto frame=seq<0?scene->EvaluateGroup():scene->EvaluateSequence(seq,elapsed);
  if(summary){
   std::cout<<"{\"ok\":true,\"bankSource\":"<<Quote(scene->AnimationSource())<<",\"animations\":[";
   for(size_t i=0;i<scene->Model().animations.size();++i){if(i)std::cout<<',';std::cout<<Quote(scene->Model().animations[i].name);}
   std::cout<<"],\"sequenceGroups\":[";for(size_t i=0;i<scene->Model().animations.size();++i){if(i)std::cout<<',';std::cout<<scene->SequenceGroup(uint32_t(i));}
   std::cout<<"],\"textures\":[";bool comma=false;for(const auto& name:scene->Textures()){if(comma)std::cout<<',';comma=true;std::cout<<Quote(name);}
   std::cout<<"]}\n";return 0;
  }
  std::cout<<std::setprecision(9)<<"{\"ok\":true,\"bankSource\":"<<Quote(scene->AnimationSource())<<",\"matchedBones\":"<<scene->Binding().matchedBones<<",\"animatedBones\":"<<scene->Binding().animatedBones<<",\"weightCoverage\":"<<scene->Binding().influencedWeightCoverage<<",\"sequenceGroups\":[";
  for(size_t i=0;i<scene->Model().animations.size();++i){if(i)std::cout<<',';std::cout<<scene->SequenceGroup(static_cast<uint32_t>(i));}
  std::cout<<"],\"meshToBank\":[";for(size_t i=0;i<scene->Binding().meshToBank.size();++i){if(i)std::cout<<',';std::cout<<scene->Binding().meshToBank[i];}
  std::cout<<"],\"world\":[";for(size_t i=0;i<frame.pose.world.size();++i){if(i)std::cout<<',';std::cout<<'[';for(unsigned j=0;j<16;++j){if(j)std::cout<<',';std::cout<<frame.pose.world[i].values[j];}std::cout<<']';}
  std::cout<<"],\"textures\":[";bool comma=false;for(const auto& path:scene->Textures()){if(comma)std::cout<<',';comma=true;std::cout<<Quote(path);}
  std::cout<<"],\"batches\":[";comma=false;for(const auto& b:scene->Materials().batches){if(comma)std::cout<<',';comma=true;std::cout<<"{\"division\":"<<b.division<<",\"region\":"<<b.region<<",\"material\":"<<b.material<<",\"index\":"<<b.index<<'}';}
  std::cout<<"],\"regions\":[";comma=false;for(const auto& r:frame.regions){if(comma)std::cout<<',';comma=true;std::cout<<"{\"division\":"<<r.division<<",\"region\":"<<r.region<<",\"vertices\":[";bool cv=false;for(const auto& v:r.vertices){if(cv)std::cout<<',';cv=true;std::cout<<"{\"p\":";Point(v.position);std::cout<<",\"n\":";Point(v.normal);std::cout<<",\"tangent\":";Point(v.tangent);std::cout<<",\"sign\":"<<v.tangentSign<<",\"color\":[";for(unsigned c=0;c<4;++c){if(c)std::cout<<',';std::cout<<unsigned(v.color[c]);}std::cout<<"],\"uv\":[";for(size_t i=0;i<v.uv.size();++i){if(i)std::cout<<',';std::cout<<v.uv[i];}std::cout<<"]}";}std::cout<<"],\"indices\":[";for(size_t i=0;i<r.indices.size();++i){if(i)std::cout<<',';std::cout<<r.indices[i];}std::cout<<"]}";}
  std::cout<<"]}\n";return 0;
 }catch(const std::exception& e){std::cout<<"{\"ok\":false,\"error\":"<<Quote(e.what())<<"}\n";return 1;}
}
