#include "ASECM3/Semantics.h"
#include <algorithm>
#include <cctype>
#include <set>
namespace ASECM3 {
namespace {
struct NameID { const char* name; int32_t id; };
struct ActorState { const char* hero; const char* token; };
struct ActorQuery { const char* hero; const char* tokens; bool weapon; };
struct Policy { const char* model; const char* name; int32_t index,id; bool excluded,global,hasExcluded,hasGlobal; uint32_t weight; bool weighted; };
#include "Semantics.generated.inc"
std::string Lower(std::string text){for(auto& c:text)c=char(std::tolower(static_cast<unsigned char>(c)));return text;}
std::string Normalize(const std::string& text){std::string out;for(unsigned char c:text)if(c<128&&std::isalnum(c))out+=char(std::tolower(c));return out;}
std::string ModelIdentity(std::string model){
    const auto slash=model.find_last_of("/\\");if(slash!=std::string::npos)model.erase(0,slash+1);
    const auto dot=model.find_last_of('.');if(dot!=std::string::npos)model.resize(dot);
    return model;
}
bool Prefix(const std::string& s,const char* prefix){return s.rfind(prefix,0)==0;}
int32_t Exact(const std::string& name){for(const auto& e:wrathNames)if(name==e.name)return e.id;return -1;}
bool Known(const std::string& part){for(const auto* token:knownTokens)if(part==token)return true;return false;}
bool Upper(char c){return c>='A'&&c<='Z';}
bool LowerCase(char c){return c>='a'&&c<='z';}
bool Digit(char c){return c>='0'&&c<='9';}
std::vector<std::string> Tokens(const std::string& name){
    std::vector<std::string> result;
    for(size_t begin=0;begin<name.size();){
        if(!Upper(name[begin])&&!LowerCase(name[begin])&&!Digit(name[begin])){++begin;continue;}
        size_t end=begin+1;const bool number=Digit(name[begin]);
        while(end<name.size()&&(number?Digit(name[end]):Upper(name[end])||LowerCase(name[end])))++end;
        const auto part=name.substr(begin,end-begin);
        if(number||Known(Lower(part)))result.push_back(Lower(part));
        else for(size_t i=0;i<part.size();){
            size_t j=i+1;
            if(Upper(part[i])&&j<part.size()&&Upper(part[j])){
                while(j<part.size()&&Upper(part[j]))++j;
                if(j<part.size()&&LowerCase(part[j]))--j;
            }else while(j<part.size()&&LowerCase(part[j]))++j;
            result.push_back(Lower(part.substr(i,j-i)));i=j;
        }
        begin=end;
    }
    return result;
}
ActionMapping Result(int32_t id,const char* reason,const char* confidence){ActionMapping r;r.action=id;r.reason=reason;r.confidence=confidence;return r;}
ActionMapping Automatic(const std::string& name,const std::string& model){
    const auto normalized=Normalize(name);auto withoutNumber=normalized;
    while(!withoutNumber.empty()&&Digit(withoutNumber.back()))withoutNumber.pop_back();
    int32_t exact=Exact(normalized);if(exact<0)exact=Exact(withoutNumber);
    if(exact>=0)return Result(exact,"Exact Wrath AnimationData name","high");
    const auto tokens=Tokens(name);const std::set<std::string> values(tokens.begin(),tokens.end());
    const auto has=[&](const char* token){return values.count(token)!=0;};
    std::string hero=Lower(model);if(Prefix(hero,"storm_hero_"))hero.erase(0,11);hero=hero.substr(0,hero.find('_'));
    bool stealth=false,query=false,onlyWeapon=true;
    for(const auto& e:actorStates)if(hero==e.hero&&values.count(e.token))stealth=true;
    std::string signature;for(const auto& token:values){if(!signature.empty())signature+=' ';signature+=token;}
    for(const auto& e:actorQueries)if(hero==e.hero&&signature==e.tokens){query=true;onlyWeapon&=e.weapon;}
    bool facial=has("portrait")||has("defaultface"),mounted=false;
    for(const auto& t:values){if(t.size()>=4&&t.substr(t.size()-4)=="eyes")facial=true;if(Prefix(t,"ride")&&t!="ridenone")mounted=true;} // "Ridenone" is the unridden body clip (morph forms)
    std::string action;
    for(const auto* t:{"death","dead","spell","channel","attack","dance","victory","taunt","talk","dialogue","listen","work","gather","fidget","run","walk","stand"})if(has(t)){action=t;break;}
    ActionMapping result;bool mapped=true;
    const bool locomotion=action=="stand"||action=="walk"||action=="run";
    if(facial)result=Result(153,"Portrait/facial layer is not a body idle","low");
    else if(action=="death"||action=="dead")result=Result(action=="death"?1:6,"Death/dead action","high");
    else if(action=="dance"||action=="victory"||action=="taunt")result=Result(action=="dance"?69:action=="victory"?68:74,"Explicit emote action","high");
    else if(action=="talk"||action=="dialogue"||action=="listen"||action=="work"||action=="gather")result=Result(action=="work"||action=="gather"?62:60,"Secondary action","medium");
    else if(action=="fidget")result=Result(153,"Fidget has no direct Wrath equivalent","low");
    else if(locomotion&&(has("start")||has("end")))result=Result(153,"State transition is not looping locomotion","low");
    else if(locomotion&&mounted)result=Result(action=="stand"?91:153,"Mounted idle or rider locomotion","medium");
    else if(locomotion&&stealth)result=Result(action=="stand"?120:action=="walk"?119:223,"Body actor confirms stealth state","high");
    else if(locomotion&&has("swim"))result=Result(action=="stand"?41:has("back")||has("backwards")?45:42,"Swimming action state","high");
    else if((action=="walk"||action=="run")&&(has("back")||has("backwards")))result=Result(13,"Backward locomotion","high");
    else if(action=="walk"&&(has("fast")||has("turbo")))result=Result(5,"Explicit fast walk approximated as Run","medium");
    else if(action=="walk"&&has("slow"))result=Result(4,"Explicit slow locomotion","medium");
    else if(action=="stand"&&has("wounded"))result=Result(8,"Wounded idle","high");
    else if(action=="attack"&&has("bow"))result=Result(46,"Explicit bow attack","high");
    else if(action=="attack"&&has("rifle"))result=Result(49,"Explicit rifle attack","high");
    else if(action=="stand"&&has("ready")&&has("bow"))result=Result(29,"Explicit bow ready","high");
    else if(action=="stand"&&has("ready")&&has("rifle"))result=Result(48,"Explicit rifle ready","high");
    else if(action=="stand"&&has("ready")&&has("fly"))result=Result(25,"Flying combat ready","medium");
    else if(action=="attack")result=Result(16,"Attack; weapon style unproven","medium");
    else if(action=="run")result=Result(5,"Explicit Run action","high");
    else if(action=="spell"||action=="channel"){
        if(has("ready")||has("precast"))result=Result(has("omni")?52:51,"Spell preparation","medium");
        else if(has("channel"))result=Result(has("omni")?125:124,"Sustained channel","high");
        else if(has("omni"))result=Result(54,"Omni spell","high");
        else if(has("forward")||has("directed"))result=Result(53,"Directed spell","high");
        else if(query&&onlyWeapon)result=Result(16,"Exact body actor query uses Spell for weapon attacks","medium");
        else {bool letter=false;for(const auto& t:values)letter|=t.size()==1;result=Result(2,"Spell cast style unproven; letters are hero-specific",letter?"low":"medium");}
    }else mapped=false;
    if(mapped){
        if(has("fly"))for(const auto& e:wrathNames)if(e.id==result.action){const int32_t fly=Exact("fly"+std::string(e.name));if(fly>=0)result.action=fly;break;}
        return result;
    }
    const NameID special[]={{"glbirth",127},{"glstand",0},{"gldeath",1},{"gldead",6},{"standup",7},{"nearimpact",191},{"unburrow",224},{"burrow",201},{"gather",62}};
    for(const auto& e:special)if(Prefix(withoutNumber,e.name))return Result(e.id,"M3 special name fallback","medium");
    // Preserve the converter's legacy fallback token boundaries (no camel split).
    std::set<std::string> legacy;std::string word;
    for(size_t i=0;i<=name.size();++i){const char c=i<name.size()?name[i]:' ';if(Upper(c)||LowerCase(c))word+=char(std::tolower(static_cast<unsigned char>(c)));else if(!word.empty()){legacy.insert(word);word.clear();}}
    const auto old=[&](const char* t){return legacy.count(t)!=0;};
    if(old("fly")&&!legacy.empty()){
        std::string first;for(char c:name){if(Upper(c)||LowerCase(c))first+=char(std::tolower(static_cast<unsigned char>(c)));else if(!first.empty())break;}
        const int32_t fly=Exact("fly"+first);if(fly>=0)return Result(fly,"Action plus Fly state","high");
    }
    if(old("flail"))return Result(9,"Flail approximated as CombatWound","medium");
    if(old("hover"))return Result(old("start")?192:old("end")?200:193,"Hover state/transition","medium");
    if(old("stand")&&mounted)return Result(91,"Mounted Stand state","medium");
    if(old("stand")&&old("ready"))return Result(old("end")?89:25,"Combat ready state","medium");
    if(old("stand")&&old("cover"))return Result(old("end")?89:48,"Unproven Cover approximated as ReadyRifle","low");
    const NameID specific[]={{"jumpstart",37},{"jumpend",39},{"spellcast",32},{"spellprecast",31}};
    for(const auto& e:specific)if(Prefix(withoutNumber,e.name))return Result(e.id,"M3 specific fallback","high");
    const NameID fallback[]={{"attack",16},{"birth",127},{"blink",153},{"block",36},{"build",152},{"burrow",201},{"channel",124},{"dance",69},{"death",1},{"dead",6},{"dialogue",60},{"fidget",153},{"freeze",14},{"impact",191},{"jump",38},{"land",200},{"listen",60},{"load",105},{"morph",130},{"pickup",50},{"pose",0},{"reload",106},{"restart",7},{"run",5},{"shield",24},{"spawn",145},{"spell",2},{"stand",0},{"stun",14},{"talk",60},{"victory",68},{"walk",4},{"work",62}};
    for(const auto& e:fallback)if(Prefix(withoutNumber,e.name))return Result(e.id,"M3 leading action fallback",std::string(e.name)=="walk"?"low":"medium");
    return Result(153,"Unresolved M3 action retained as Custom0","low");
}
}
ActionMapping ResolveAction(const std::string& name,const std::string& model,uint32_t index,uint32_t flags){
    const auto sourceIdentity=ModelIdentity(model);
    auto result=Automatic(name,sourceIdentity);
    const auto normalized=Normalize(name),identity=Normalize(sourceIdentity);
    const Policy *named=nullptr,*indexed=nullptr;
    for(unsigned pass=0;pass<2;++pass)for(const auto& p:policies)
        if(p.index<0&&normalized==p.name&&(pass==0?!*p.model:identity==p.model))named=&p;
    for(const auto& p:policies)if(identity==p.model&&p.index>=0&&uint32_t(p.index)==index)indexed=&p;
    for(const auto* policy:{named,indexed}){
        if(!policy)continue;const auto& p=*policy;
        if(policy==indexed&&normalized!=p.name)throw Error("M3 source-index policy name mismatch for "+model+" clip "+std::to_string(index)+": "+name);
        if(p.id>=0){result.action=p.id;result.manual=true;result.confidence="manual";}
        if(p.hasExcluded)result.excluded=p.excluded;
        if(p.hasGlobal)result.global=p.global;
        if(p.weighted)result.weight=p.weight;
        result.reason=policy==indexed?"Manual guarded source-index policy":"Manual exact-name policy";
    }
    if(flags&4)result.global=true;
    return result;
}
std::vector<ActionMapping> ResolveActions(const std::vector<Animation>& clips,const std::string& model){
    std::vector<ActionMapping> result;result.reserve(clips.size());
    for(size_t i=0;i<clips.size();++i)result.push_back(ResolveAction(clips[i].name,model,uint32_t(i),clips[i].flags));
    return result;
}
int32_t SelectAction(const std::vector<ActionMapping>& mappings,uint32_t action){
    if(action>505)throw Error("WoW action must be in the Wrath AnimationData range 0..505");
    int32_t chosen=-1;uint32_t weight=0;
    for(size_t i=0;i<mappings.size();++i){const auto& m=mappings[i];if(m.action!=int32_t(action)||m.excluded||m.global||!m.weight)continue;
        if(chosen<0||m.weight>weight){chosen=int32_t(i);weight=m.weight;}}
    return chosen;
}
int32_t SelectBodyAction(const std::vector<ActionMapping>& mappings,uint32_t action){
    if(action>505)throw Error("WoW action must be in the Wrath AnimationData range 0..505");
    const auto exact=SelectWorldAction(mappings,action);if(exact>=0)return exact;
    // Preference lists by family; the first one with a clip wins.
    const auto first=[&](std::initializer_list<uint32_t> list){for(const auto a:list){const auto c=SelectAction(mappings,a);if(c>=0)return c;}return int32_t(-1);};
    int32_t clip=-1;
    switch(action){
    case 16:case 17:case 18:case 19:case 46:case 47:case 49:case 57:case 58:case 59:case 85:case 86:case 87:case 88:case 95:case 117:
        clip=first({16,17,18,19,87,85,46,49,57,58,2,32});break;
    case 25:case 26:case 27:case 28:case 29:case 48:case 51:case 52:case 56:
        clip=first({25,26,27,28,29,48,51,52,56});break;
    case 2:case 31:case 32:case 33:case 53:case 54:case 55:case 124:
        clip=first({2,32,31,33,53,54,55,124,16});break;
    case 6:case 7:clip=first({1,6});break;
    case 13:case 92:case 93:case 143:case 42:case 43:case 44:case 45:clip=first({4,5});break;
    default:break;
    }
    if(clip>=0)return clip;
    return SelectAction(mappings,0);
}
int32_t SelectWorldAction(const std::vector<ActionMapping>& mappings,uint32_t action){
    const auto exact=SelectAction(mappings,action);if(exact>=0)return exact;
    // Some HotS banks contain one body locomotion clip (Walk A) and no Run.
    // Retain its source meaning; this is an explicit world playback fallback.
    if(action==143){const auto run=SelectAction(mappings,5);if(run>=0)return run;}
    if(action==5||action==143)return SelectAction(mappings,4);
    return -1;
}
}
