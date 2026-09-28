#include "../ezorsia/stdafx.h"
#include "../ezorsia/EvanRuntime.h"
#include "../ezorsia/EvanPursuit.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdio>
#include <cstdlib>
extern "C" bool TestPursuitReceive(const unsigned char*, unsigned long, unsigned);
extern "C" int TestPursuitSelect(int, void*, RECT*, void**, unsigned);
static void Check(bool ok, const char* message) { if (!ok) { printf("FAIL %s\n",message); std::exit(1); } }
static void Jump(DWORD address, void* target) {
    auto p = reinterpret_cast<unsigned char*>(address); p[0]=0xe9;
    *reinterpret_cast<DWORD*>(p+1)=reinterpret_cast<DWORD>(target)-address-5;
}
static int __cdecl ReadSecure(int* value, int) { return *value; }
static int __fastcall IsBlocked(unsigned char* mob, void*) { return *reinterpret_cast<int*>(mob+0x600); }
static RECT* __fastcall GetMobRect(unsigned char* mob, void*, RECT* out, int) {
    *out = *reinterpret_cast<RECT*>(mob+0x610); return out;
}
struct Position { void** table; POINT value; };
static const POINT* __fastcall GetPosition(Position* p, void*) { return &p->value; }
static void Put(unsigned char* p, unsigned value) { std::memcpy(p,&value,4); }
static void Receive(unsigned oid, unsigned duration, unsigned tick) {
    unsigned char packet[23]{}; packet[4]=0x0b;packet[5]=0x10;packet[6]=1;
    Put(packet+7,oid);Put(packet+11,duration);Put(packet+15,1600);Put(packet+19,1000);
    Check(TestPursuitReceive(packet,sizeof(packet),tick),"private packet consumed");
}
// Exercise the installed x86 gate with the original caller's frame layout.
__declspec(naked) static int Gate(int skill, void* position, RECT* rect, void** selected, int facing) {
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        push esi
        push edi
        sub esp,140h
        mov ebx,ebp
        lea ebp,[esp+130h]
        mov eax,[ebx+8]
        mov [ebp-14h],eax
        mov eax,[ebx+0ch]
        mov [ebp-70h],eax
        mov eax,[ebx+18h]
        mov [ebp-5ch],eax
        mov esi,[ebx+10h]
        lea edi,[ebp-58h]
        movsd
        movsd
        movsd
        movsd
        mov eax,109565a1h
        call eax
        mov edx,[ebx+14h]
        mov ecx,[ebp-11ch]
        mov [edx],ecx
        mov ebp,ebx
        lea esp,[ebp-0ch]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
int main(int argc,char** argv) {
    Check(argc==2,"exe argument");
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});
    Check(bytes.size()>4096,"read native executable");
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(bytes.data()+dos->e_lfanew);
    auto image=static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(0x10400000),nt->OptionalHeader.SizeOfImage,
            MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(image==reinterpret_cast<void*>(0x10400000),"map isolated native image");
    auto sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
        std::memcpy(image+sections[i].VirtualAddress,bytes.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
    auto site=reinterpret_cast<unsigned char*>(0x109565a1);const auto original=*site;
    *site=0;Check(!EvanRuntime::Install(),"bad pursuit signature rejects installation");
    *site=original;Check(EvanRuntime::Install(),"runtime installation");

    // Keep the real CMobPool selection/filter instructions; stub only runtime
    // dependencies (secure scalar decoding, geometry, and Win32 imports).
    Jump(0x10508a95,reinterpret_cast<void*>(&ReadSecure));
    Jump(0x104e8152,reinterpret_cast<void*>(&ReadSecure));
    Jump(0x10670c06,reinterpret_cast<void*>(&IsBlocked));
    Jump(0x10664559,reinterpret_cast<void*>(&GetMobRect));
    // Relocate the two absolute IAT operands into the isolated image.
    *reinterpret_cast<DWORD*>(0x106785af)=0x10bf04ac;
    *reinterpret_cast<DWORD*>(0x106785c4)=0x10bf04a8;
    *reinterpret_cast<void**>(0x10bf04ac)=reinterpret_cast<void*>(&IsRectEmpty);
    *reinterpret_cast<void**>(0x10bf04a8)=reinterpret_cast<void*>(&IntersectRect);
    unsigned char pool[64]{}, nodeA[32]{}, nodeB[32]{}, a[0x640]{}, b[0x640]{}, info[0x220]{};
    *reinterpret_cast<void**>(pool+0x28)=nodeA+16;
    *reinterpret_cast<void**>(nodeA+4)=nodeB;
    *reinterpret_cast<void**>(nodeA+20)=a;
    *reinterpret_cast<void**>(nodeB+20)=b;
    *reinterpret_cast<void**>(0x10bebfa4)=pool;
    for(auto mob:{a,b}) {
        Put(mob+0x124,1);*reinterpret_cast<void**>(mob+0x188)=info;
        Put(mob+0x370,10);Put(mob+0x374,22161002);
    }
    Put(a+0x17c,70001);Put(b+0x17c,70002);
    *reinterpret_cast<RECT*>(a+0x610)={-500,-150,-450,-100};
    *reinterpret_cast<RECT*>(b+0x610)={500,-650,600,-550};
    void* vtable[5]{};vtable[4]=reinterpret_cast<void*>(&GetPosition);
    Position position{vtable,{-570,-95}};
    RECT rect{-970,-203,-555,-80};void* selected=nullptr;
    Check(TestPursuitSelect(22171002,&position,&rect,&selected,100)==-1,"no private mark, even with shared curse");
    Receive(70002,30000,100);
    Check(TestPursuitSelect(22171002,&position,&rect,&selected,101)==1 && selected==b,"diagonal remote target beats nearer decoy");
    Check(rect.left==-2170 && rect.top==-1095 && rect.right==1030 && rect.bottom==905,"bounded symmetric range");
    Check(TestPursuitSelect(22171003,&position,&rect,&selected,101)==-1,"other skills unchanged");
    Check(TestPursuitSelect(22171002,&position,&rect,&selected,30100)==-1,"expiry");
    for(unsigned offset:{0x450,0x328,0x600}) {
        Put(b+offset,1);Check(TestPursuitSelect(22171002,&position,&rect,&selected,101)==-1,"native hidden/invulnerable/blocked checks");Put(b+offset,0);
    }
    Put(b+0x124,0);Check(TestPursuitSelect(22171002,&position,&rect,&selected,101)==-1,"native dead target excluded");Put(b+0x124,1);
    Put(b+0x370,0);Check(TestPursuitSelect(22171002,&position,&rect,&selected,101)==-1,"lost curse");Put(b+0x370,10);
    position.value.x=-2000;Check(TestPursuitSelect(22171002,&position,&rect,&selected,101)==-1,"maximum range");position.value.x=-570;
    Receive(70001,5000,200);Check(TestPursuitSelect(22171002,&position,&rect,&selected,201)==1 && selected==a,"replace target");
    Receive(70002,5000,0xfffffff0);Check(TestPursuitSelect(22171002,&position,&rect,&selected,10)==1,"tick rollover");
    unsigned char removal[10]{};removal[4]=0xed;Put(removal+6,70002);
    Check(!TestPursuitReceive(removal,10,11),"removal also reaches native handler");
    Check(TestPursuitSelect(22171002,&position,&rect,&selected,12)==-1,"removal clears mark");
    Receive(70002,30000,100);unsigned char malformed[7]{0,0,0,0,0x0b,0x10,1};
    Check(TestPursuitReceive(malformed,7,101),"truncated custom packet consumed");
    Check(TestPursuitSelect(22171002,&position,&rect,&selected,102)==-1,"malformed packet fails closed");
    Receive(70002,30000,GetTickCount());
    // Endpoints expose the actual gate's selected/fallback path and stack ABI.
    const unsigned char success[]={0x8b,0xc6,0xc3},fallback[]={0x33,0xc0,0xc3};
    std::memcpy(reinterpret_cast<void*>(0x109565c9),success,sizeof(success));
    std::memcpy(reinterpret_cast<void*>(0x109565a7),fallback,sizeof(fallback));
    for(int facing:{0,1}) Check(Gate(22171002,&position,&rect,&selected,facing)==1 && selected==b,"installed gate in both facings");
    Check(Gate(22171003,&position,&rect,&selected,0)==0,"installed gate fallback for other skill");
    EvanPursuit::Reset();Check(Gate(22171002,&position,&rect,&selected,0)==0,"field/death reset fallback");
    puts("PASS pursuit: actual native target filtering, diagonal/back-facing gate, bounded range, independent mark, lifecycle, malformed packet and x86 ABI");
}
