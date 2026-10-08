#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <xmmintrin.h>
#include "../ezorsia/MobInitializationFix.h"
namespace CrashReporter { void RecordEvent(const char*,const char*,...) {} }
static void Require(bool ok,const char* message) {
    if (!ok) { printf("FAIL %s\n",message); exit(1); }
}
constexpr DWORD Delta=0x30000000;
static DWORD N(DWORD address) { return address+Delta; }
static BYTE* Map(const char* path) {
    FILE* f=nullptr;Require(!fopen_s(&f,path,"rb"),"open image");
    fseek(f,0,SEEK_END);std::vector<BYTE> bytes(ftell(f));rewind(f);
    Require(fread(bytes.data(),1,bytes.size(),f)==bytes.size(),"read image");fclose(f);
    const auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    const auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes.data()+dos->e_lfanew);
    auto* base=static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(N(0x400000)),nt->OptionalHeader.SizeOfImage,
        MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));Require(base!=nullptr,"map image");
    memcpy(base,bytes.data(),nt->OptionalHeader.SizeOfHeaders);
    const auto* sec=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
        memcpy(base+sec[i].VirtualAddress,bytes.data()+sec[i].PointerToRawData,sec[i].SizeOfRawData);
    return base;
}
static void Jump(DWORD from,void* to) {
    auto* p=reinterpret_cast<BYTE*>(from);p[0]=0xe9;
    const DWORD rel=reinterpret_cast<DWORD>(to)-from-5;memcpy(p+1,&rel,4);
    FlushInstructionCache(GetCurrentProcess(),p,5);
}
// Only the encrypted template accessor is substituted. The complete native
// movement-selection function executes, including its real callee cleanup.
static int __cdecl TemplateMove(int* value,int) { return *value; }
static DWORD nativeFunction=N(0x66b599), callbackSite=N(0x9b13e4);
static BYTE mob[0x600],firstVector[0x600],secondVector[0x600],monsterTemplate[0x240];
static DWORD vtable[2], parentFrame[6],grandFrame[6];
static void* owner=mob+4;
static void* vectorPointer=firstVector;
static DWORD framePointer;
static int direction,action,result;
__declspec(naked) static void ReturnFromCallbackSite() { __asm { ret } }
__declspec(naked) static int InvokeInitialization() {
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov ebp,framePointer
        mov ecx,owner
        lea eax,vtable
        push offset finished
        push vectorPointer
        push action
        push 0
        push direction
        jmp dword ptr [callbackSite]
    finished:
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
static int Invoke(bool initialization) {
    auto function=reinterpret_cast<int(__thiscall*)(void*,int,int,int,void*)>(nativeFunction);
    return initialization ? InvokeInitialization() : function(owner,direction,0,action,vectorPointer);
}
static bool Faults(bool initialization) {
    __try { Invoke(initialization); }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
int main(int argc,char** argv) {
    Require(argc==2,"client path");BYTE* base=Map(argv[1]);
    Jump(N(0x416563),TemplateMove);
    *reinterpret_cast<void**>(mob+0x118)=firstVector+12;
    *reinterpret_cast<void**>(mob+0x188)=monsterTemplate;
    vtable[1]=nativeFunction;
    grandFrame[1]=N(0x662a32);parentFrame[0]=reinterpret_cast<DWORD>(grandFrame);
    parentFrame[1]=N(0x9bbd5d);framePointer=reinterpret_cast<DWORD>(parentFrame);
    direction=0;action=1;
    Require(Faults(false),"baseline null vector really faults");
    const BYTE saved=base[0x26b599];base[0x26b599]=0x90;
    Require(!MobInitializationFix::Install(reinterpret_cast<HMODULE>(base)),"reject incompatible entry");
    base[0x26b599]=saved;
    Require(MobInitializationFix::Install(reinterpret_cast<HMODULE>(base)),"install verified patch");
    Require(MobInitializationFix::Install(reinterpret_cast<HMODULE>(base)),"idempotent install");
    // Verified native CALL [eax+4] provides the real owner return address.
    // The outer EBP frames model the inspected vector/CMob init return sites.
    Jump(N(0x9b13e7),ReturnFromCallbackSite);
    unsigned comparisons=0;
    for(int move=0;move<4;++move) for(int stance: {0,1,4,5,6,7,10,11}) for(int dir: {-1,0,1}) {
        *reinterpret_cast<int*>(monsterTemplate+0x40)=move;
        action=stance;direction=dir;
        *reinterpret_cast<void**>(mob+0x11c)=secondVector+12;
        const int initialized=Invoke(false);
        const unsigned csr=_mm_getcsr();
        *reinterpret_cast<void**>(mob+0x11c)=nullptr;
        Require(Invoke(true)==initialized,"initial null vector matches zero-initialized vector action");
        Require(_mm_getcsr()==csr,"floating state preserved");++comparisons;
    }
    Require(Faults(false),"unrelated null callback still faults");
    parentFrame[1]++;Require(Faults(true),"wrong caller is not suppressed");parentFrame[1]--;
    vectorPointer=secondVector;Require(Faults(true),"wrong first vector is not suppressed");vectorPointer=firstVector;
    *reinterpret_cast<void**>(mob+0x11c)=secondVector+12;
    *reinterpret_cast<int*>(secondVector+0x250)=1;
    *reinterpret_cast<int*>(monsterTemplate+0x204)=1;
    *reinterpret_cast<int*>(monsterTemplate+0x40)=1;direction=1;action=0;
    Require(Invoke(false)==32 && Invoke(true)==32,"active second-vector behavior retained");
    Require(*reinterpret_cast<void**>(mob+0x11c)==secondVector+12,"does not replace owned vector");
    printf("PASS mob initialization: %u native action comparisons, baseline/rejection/propagation/active-vector checks\n",comparisons);
}
