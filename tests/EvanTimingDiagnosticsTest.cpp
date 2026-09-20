#include "../ezorsia/stdafx.h"
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include "../ezorsia/EvanTimingDiagnostics.h"
static std::vector<std::string> lines;
void ClientLog::Append(Component, const char* format, ...) {
    char buffer[2048]; va_list args; va_start(args,format);
    vsnprintf(buffer,sizeof(buffer),format,args); va_end(args); lines.emplace_back(buffer);
}
bool Memory::SetHook(bool,void**,void*) { return false; }
static int state=-1, calls=0;
static int __fastcall State(void*,void*) { return state; }
static void __fastcall Prepare(void* avatar,void*,int,int,int) {
    ++calls; state=150;
    *reinterpret_cast<int*>(static_cast<char*>(avatar)+0x4f8)=2010;
}
static int __fastcall Attack(void* user,void*,void*,int,int,int) {
    ++calls;
    EvanTimingDiagnostics::PrepareHook(static_cast<char*>(user)+0x88,nullptr,6,100,0);
    EvanTimingDiagnostics::HitDelay(9000,2010,6,1170,2010);
    EvanTimingDiagnostics::QueuedHit(3,420,1368);
    return 1;
}
static int __fastcall Buff(void* user,void*,void*,int,int,int,int,int,int) {
    ++calls;
    EvanTimingDiagnostics::PrepareHook(static_cast<char*>(user)+0x88,nullptr,6,100,0);
    return 1;
}
static void __fastcall Update(void*,void*) { ++calls; }
static bool Has(const char* text) { for(auto& s:lines) if(s.find(text)!=std::string::npos)return true;return false; }
int main() {
    using namespace EvanTimingDiagnostics;
    attack=reinterpret_cast<EvanTimingDiagnostics::Attack>(&::Attack);
    buff=reinterpret_cast<EvanTimingDiagnostics::Buff>(&::Buff);
    prepare=reinterpret_cast<EvanTimingDiagnostics::Prepare>(&::Prepare);
    update=reinterpret_cast<EvanTimingDiagnostics::Update>(&::Update);
    action=reinterpret_cast<Action>(&State);
    alignas(4) char user[0xd00]{}; int skill=22171002;
    if(AttackHook(user,nullptr,&skill,30,0,0)!=1 || calls!=2)return 1;
    if(!Has("phase=illusion_hit_schedule") || !Has("c=1170") || !Has("phase=prepare_end"))return 2;
    if(!Has("phase=illusion_hit_queued") || !Has("a=3 b=420 c=1368"))return 11;
    auto unscopedCount=lines.size();QueuedHit(0,0,948);
    if(lines.size()!=unscopedCount)return 12;
    auto count=lines.size();
    auto beforeCalls=calls;
    AttackHook(user,nullptr,&skill,30,0,0);
    if(lines.size()!=count || calls!=beforeCalls+2 || suppressed!=1)return 9;
    *reinterpret_cast<int*>(user+0x88+0x4f8+0x5dc)=1629;
    if(Duration(user+0x88)!=1629)return 10;
    for(int i=0;i<100;++i) UpdateHook(user,nullptr);
    if(lines.size()!=count)return 3; // No per-frame writes.
    state=-1;UpdateHook(user,nullptr);
    if(!Has("phase=action_clear_observed") || current.pending)return 4;
    skill=22141002;BuffHook(user,nullptr,&skill,20,0,0,0,0,0);
    if(!Has("skill=22141002") || !current.pending)return 5;
    count=lines.size();skill=1121000;BuffHook(user,nullptr,&skill,20,0,0,0,0,0);
    if(lines.size()!=count)return 6;
    records=512;count=lines.size();Record(current,"test",0,0,0);Record(current,"test",0,0,0);
    if(lines.size()!=count+1 || !Has("event=evan_timing_limit"))return 7;
    try { Cast nested; EntryScope scope(nested); throw 1; } catch(int) {}
    if(entering!=nullptr)return 8;
    puts("PASS attack/buff/prepare/clear events, original calls retained, unrelated skills excluded, no per-frame writes, session cap");
}
