#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../ezorsia/TargetedCrashSnapshot.cpp"

using namespace TargetedCrashSnapshot;
static Snapshot saved{};
static unsigned calls=0;
static bool failing=false;
static void Require(bool ok,const char* why) {
    if (!ok) { printf("FAIL %s error=%lu\n",why,GetLastError()); exit(1); }
}
static void Writer(const Snapshot& s) {
    saved=s; ++calls; SetLastError(321);
    if (failing) RaiseException(0xe0000042,0,0,nullptr);
}
static bool Copied(DWORD address,const void* expected,unsigned size) {
    const BYTE* input=static_cast<const BYTE*>(expected);
    for (unsigned offset=0;offset<size;++offset) {
        bool found=false;
        for (unsigned i=0;i<saved.memoryCount;++i) {
            const auto& b=saved.memory[i];
            if (address+offset>=b.address && address+offset-b.address<b.copied) {
                if (b.bytes[address+offset-b.address]!=input[offset]) return false;
                found=true; break;
            }
        }
        if (!found) return false;
    }
    return true;
}
static BYTE* Map(const wchar_t* path) {
    FILE* file=nullptr;Require(!_wfopen_s(&file,path,L"rb"),"open native EXE");
    fseek(file,0,SEEK_END);std::vector<BYTE> bytes(ftell(file));rewind(file);
    Require(fread(bytes.data(),1,bytes.size(),file)==bytes.size(),"read native EXE");fclose(file);
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes.data()+dos->e_lfanew);
    Require(nt->FileHeader.TimeDateStamp==0x4b7c15c9 && nt->OptionalHeader.SizeOfImage==0xa94000,"known v83 image");
    auto image=static_cast<BYTE*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Require(image!=nullptr,"offline map");memcpy(image,bytes.data(),nt->OptionalHeader.SizeOfHeaders);
    auto sections=IMAGE_FIRST_SECTION(nt);
    for (unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
        memcpy(image+sections[i].VirtualAddress,bytes.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
    // No entry point/import initialization. Only 00471BB5, a position-independent
    // table lookup with no calls or globals, executes below.
    return image;
}
static void ResetHarnessBudget() {
    claimed=0; snapshots[static_cast<unsigned>(Family::CashCategory)]={};
}
int wmain(int argc,wchar_t** argv) {
    Require(argc==2,"native EXE path");SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    BYTE* image=Map(argv[1]);const DWORD base=reinterpret_cast<DWORD>(image);
    // Pin the disassembled throw and caller operands independently of fixtures.
    Require(image[0x722aa]==0xb8 && *reinterpret_cast<DWORD*>(image+0x722ab)==0x22000006
        && image[0x722c2]==0xe8 && image[0x6840c]==0xe8,"native mismatch throw/caller signatures");
    BYTE context[0x3700]{},owner[0x80]{},commodity[0x50]{},stack[0x500]{};
    DWORD categories[1+30*7]{},rows[1+2*2]{};
    categories[0]=30;
    for (unsigned i=0;i<30;++i) { categories[1+i*7]=2;categories[2+i*7]=i; }
    *reinterpret_cast<DWORD*>(owner+0x44)=reinterpret_cast<DWORD>(categories+1);
    using Lookup=int(__thiscall*)(void*,int,int);
    const auto lookup=reinterpret_cast<Lookup>(image+0x71bb5);
    Require(lookup(owner,2,0)==0 && lookup(owner,2,29)==29 && lookup(owner,2,99)==-1,"actual native category lookup and missing category");
    const DWORD sn=29900123; // category 2, subcategory 99, absent from fixture
    Require(lookup(owner,sn/10000000%10,sn/100000%100)==-1,"same native SN formula reaches missing classification");
    *reinterpret_cast<DWORD*>(commodity+0xc)=sn;
    rows[0]=2; rows[4]=reinterpret_cast<DWORD>(commodity);
    *reinterpret_cast<DWORD*>(context+0x36e0)=reinterpret_cast<DWORD>(rows+1);
    *reinterpret_cast<DWORD*>(image+0x7e7918)=reinterpret_cast<DWORD>(context);
    auto inner=reinterpret_cast<DWORD*>(stack+0x100),outer=reinterpret_cast<DWORD*>(stack+0x200);
    inner[0]=reinterpret_cast<DWORD>(outer);inner[1]=base+0x722c7;
    outer[1]=base+0x68411;outer[-5]=reinterpret_cast<DWORD>(owner);outer[-4]=1;
    outer[-7]=0x22000006; // Original native code overwrites this category local.
    CONTEXT ctx{};ctx.Ebp=reinterpret_cast<DWORD>(inner);ctx.Esp=reinterpret_cast<DWORD>(stack);ctx.Eip=base+0x722c7;
    EXCEPTION_RECORD rec{};rec.ExceptionCode=0xe06d7363;rec.NumberParameters=3;rec.ExceptionInformation[0]=0x19930520;
    rec.ExceptionAddress=reinterpret_cast<void*>(ctx.Eip);EXCEPTION_POINTERS info{&rec,&ctx};
    ObserveCashCategoryCpp(&info,0x22000006);Require(!calls,"disabled observer captures nothing");
    Require(Install(reinterpret_cast<HMODULE>(image),Writer),"enable observer");
    ObserveCashCategoryCpp(&info,0x22000002);Require(!calls,"other terminate errors excluded");
    inner[1]++;ObserveCashCategoryCpp(&info,0x22000006);Require(!calls,"other native throw site excluded");inner[1]--;
    outer[1]++;ObserveCashCategoryCpp(&info,0x22000006);Require(!calls,"other constructor excluded");outer[1]--;
    SetLastError(123);ObserveCashCategoryCpp(&info,0x22000006);
    Require(calls==1 && saved.family==Family::CashCategory && GetLastError()==123,"first capture and LastError preserved");
    Require(saved.memoryCount==8 && saved.version==1 && sizeof(saved)==3688,"existing BDS1 ABI and memory bound");
    Require(Copied(reinterpret_cast<DWORD>(commodity),commodity,sizeof(commodity)),"original commodity and SN retained");
    Require(Copied(reinterpret_cast<DWORD>(categories),categories,sizeof(categories)),"all 30 native categories retained across blocks");
    Require(Copied(reinterpret_cast<DWORD>(outer)-0x1c,outer-7,0x1c),"index and overwritten local retained");
    Require(lookup(owner,2,99)==-1 && *reinterpret_cast<DWORD*>(commodity+0xc)==sn,"capture does not mutate native data");
    ObserveCashCategoryCpp(&info,0x22000006);Require(calls==1,"one sample per session");
    ResetHarnessBudget();outer[-5]=MAXDWORD-8;rows[0]=MAXDWORD;
    failing=true;SetLastError(123);ObserveCashCategoryCpp(&info,0x22000006);
    Require(calls==2 && saved.memoryCount<=8 && !capturing && GetLastError()==123,"invalid pointers/counts and failing writer isolated");
    ResetHarnessBudget();outer[-5]=reinterpret_cast<DWORD>(owner);rows[0]=2;outer[-4]=MAXDWORD;
    failing=false;ObserveCashCategoryCpp(&info,0x22000006);
    Require(calls==3 && !Copied(reinterpret_cast<DWORD>(commodity),commodity,sizeof(commodity)),"invalid item index never dereferenced");
    Require(Copied(reinterpret_cast<DWORD>(categories),categories,sizeof(categories)),"categories survive missing commodity evidence");
    ResetHarnessBudget();outer[-4]=1;categories[0]=1000;ObserveCashCategoryCpp(&info,0x22000006);
    Require(calls==4 && saved.memoryCount==8,"large category count remains bounded");
    puts("PASS native category lookup, exact scope, SN + all 30 categories, partial memory, writer failure, dedup, unchanged data/LastError/BDS1");
}
