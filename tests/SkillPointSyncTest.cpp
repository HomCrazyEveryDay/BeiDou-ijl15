#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../ezorsia/SkillPointSync.h"
#include "../ezorsia/SkillPointSyncState.h"
#include "../ezorsia/ClientLog.h"

void ClientLog::Append(Component, const char*, ...) {}
static void Put(std::vector<unsigned char>& p, unsigned at, unsigned value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) p[at + i] = static_cast<unsigned char>(value >> (8 * i));
}
static std::vector<unsigned char> Login(int id, int job) {
    std::vector<unsigned char> p(110);
    Put(p,4,0x7d,2); p[11]=1;
    for (unsigned i=26;i<34;++i) p[i]=0xff;
    Put(p,35,id,4); Put(p,87,job,2);
    return p;
}
static std::vector<unsigned char> Snapshot(int id=59, int job=2112) {
    std::vector<unsigned char> p(21);
    Put(p,4,0x100c,2);p[6]=1;Put(p,7,id,4);Put(p,11,job,2);
    const unsigned points[]={576,515,394,243};
    for(unsigned i=0;i<4;++i)Put(p,13+i*2,points[i],2);
    return p;
}
static constexpr DWORD Native(DWORD address) { return address + 0x20000000; }
static void* Map(DWORD address, unsigned size) {
    address=Native(address);
    void* result=VirtualAlloc(reinterpret_cast<void*>(address),size,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    assert(result==reinterpret_cast<void*>(address));
    return result;
}
static void ReturnAt(DWORD address,int value) {
    address=Native(address);
    auto p=reinterpret_cast<unsigned char*>(address);p[0]=0xb8;std::memcpy(p+1,&value,4);p[5]=0xc3;
}
static int InvokeDisplay(DWORD address,void* window) {
    address=Native(address);
    int result;
    __asm {
        push esi
        mov esi,window
        lea eax, resumed
        push eax
        push 222
        push 111
        jmp address
    resumed:
        mov result,eax
        pop esi
    }
    return result;
}
static int InvokeClick(void* skill) {
    int result;
    DWORD address=Native(0x008acffc);
    __asm {
        push ebx
        mov ebx,skill
        call address
        mov result,eax
        pop ebx
    }
    return result;
}
int main() {
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    using namespace SkillPointSync;
    auto login=Login(59,2112), packet=Snapshot();
    State state;
    state.Observe(login.data(),login.size());
    assert(state.Receive(packet.data(),packet.size()) && state.ready);
    assert(state.ForStage(1,-1)==576 && state.ForStage(4,-1)==243 && state.ForStage(0,6)==6);
    for(unsigned n=6;n<packet.size();++n) {
        assert(state.Receive(packet.data(),n) && !state.ready);
    }
    auto bad=Snapshot(60);assert(state.Receive(bad.data(),bad.size()) && !state.ready);
    bad=Snapshot(59,112);assert(state.Receive(bad.data(),bad.size()) && !state.ready);
    bad=packet;Put(bad,19,600,2);assert(state.Receive(bad.data(),bad.size()) && !state.ready);
    bad=packet;bad[6]=2;assert(state.Receive(bad.data(),bad.size()) && !state.ready);
    assert(state.Receive(packet.data(),packet.size()) && state.ready);
    auto evan=Login(743,2218);state.Observe(evan.data(),evan.size());assert(!state.ready);
    assert(state.Receive(packet.data(),packet.size()) && !state.ready);
    auto warrior=Login(59,2100);state.Observe(warrior.data(),warrior.size());
    std::vector<unsigned char> job(14);Put(job,4,0x1f,2);Put(job,7,0x30,4);job[11]=30;Put(job,12,2110,2);
    state.Observe(job.data(),job.size());assert(state.job==2110 && !state.ready);
    for(unsigned n=0;n<login.size();++n){State truncated;truncated.Observe(login.data(),n);}

    // Execute the actual x86 caves against fixed-address native stubs. This checks
    // stack cleanup, ESI/EBX preservation, fallback and all branch continuations.
    Map(0x008a0000,0x10000);Map(0x00470000,0x10000);Map(0x00bf0000,0x2000);
    const unsigned char draw[]={0xe8,0xc6,0x82,0xbc,0xff};
    const unsigned char button[]={0xe8,0x72,0x6e,0xbc,0xff};
    const unsigned char click[]={0x83,0x7b,0x2c,0,0x0f,0x85,0x2b,0x02,0,0};
    std::memcpy(reinterpret_cast<void*>(Native(0x8ac412)),draw,5);
    std::memcpy(reinterpret_cast<void*>(Native(0x8ad866)),button,5);
    std::memcpy(reinterpret_cast<void*>(Native(0x8acffc)),click,10);
    *reinterpret_cast<unsigned char*>(Native(0x8ac412))=0xcc;
    assert(!Install());assert(*reinterpret_cast<unsigned char*>(Native(0x8ad866))==0xe8);
    *reinterpret_cast<unsigned char*>(Native(0x8ac412))=0xe8;
    const unsigned char cleanup[]={0x83,0xc4,8,0xc3};
    std::memcpy(reinterpret_cast<void*>(Native(0x8ac417)),cleanup,4);
    std::memcpy(reinterpret_cast<void*>(Native(0x8ad86b)),cleanup,4);
    ReturnAt(0x4746dd,576);ReturnAt(0x8ad006,100);ReturnAt(0x8ad227,200);ReturnAt(0x8ad231,300);
    DWORD oldProtection;
    assert(VirtualProtect(reinterpret_cast<void*>(Native(0x8ac000)),0x2000,PAGE_EXECUTE_READ,&oldProtection));
    assert(Install());
    for (DWORD page : {Native(0x8ac000), Native(0x8ad000)}) {
        MEMORY_BASIC_INFORMATION info{};
        assert(VirtualQuery(reinterpret_cast<void*>(page),&info,sizeof(info)));
        assert(info.Protect==PAGE_EXECUTE_READ);
    }
    HandlePacket(login.data(),static_cast<unsigned long>(login.size()));
    HandlePacket(packet.data(),static_cast<unsigned long>(packet.size()));
    unsigned char window[0x600]{},tab[0x50]{},skill[0x30]{};
    *reinterpret_cast<void**>(window+0x5b8)=tab;
    for(int stage=1;stage<=4;++stage) {
        *reinterpret_cast<int*>(tab+0x3c)=stage;
        const int expected[]={576,515,394,243};
        assert(InvokeDisplay(0x8ac412,window)==expected[stage-1]);
        assert(InvokeDisplay(0x8ad866,window)==expected[stage-1]);
    }
    *reinterpret_cast<int*>(skill)=21120004;assert(InvokeClick(skill)==200);
    *reinterpret_cast<int*>(skill+0x2c)=1;assert(InvokeClick(skill)==300);
    *reinterpret_cast<int*>(skill+0x2c)=0;
    Put(packet,19,0,2);HandlePacket(packet.data(),static_cast<unsigned long>(packet.size()));
    assert(InvokeClick(skill)==300);
    *reinterpret_cast<int*>(skill)=21000000;assert(InvokeClick(skill)==200);
    Reset();*reinterpret_cast<int*>(skill)=21120004;assert(InvokeClick(skill)==100);
    assert(InvokeDisplay(0x8ac412,window)==576);
    puts("PASS SP wire validation, identity reset, ordinary/Evan isolation, native x86 display/button/click caves");
}
