#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../ezorsia/InventoryRefreshFix.h"
#include "../ezorsia/CharacterSelectFocusFix.h"

namespace CrashReporter { void RecordEvent(const char*,const char*,...) {} }
constexpr DWORD Relocation = 0x30000000;
static DWORD N(DWORD address) { return address+Relocation; }
static void Require(bool ok,const char* message) {
    if (!ok) { printf("FAIL %s\n",message); exit(1); }
}
static BYTE* Map(const char* path) {
    FILE* file=nullptr; Require(!fopen_s(&file,path,"rb"),"open client image");
    fseek(file,0,SEEK_END); std::vector<BYTE> bytes(ftell(file)); rewind(file);
    Require(fread(bytes.data(),1,bytes.size(),file)==bytes.size(),"read client image"); fclose(file);
    const auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    const auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes.data()+dos->e_lfanew);
    auto* base=static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(N(0x400000)),nt->OptionalHeader.SizeOfImage,
        MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE)); Require(base!=nullptr,"map without executing client entry point");
    memcpy(base,bytes.data(),nt->OptionalHeader.SizeOfHeaders);
    const auto* sec=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
        memcpy(base+sec[i].VirtualAddress,bytes.data()+sec[i].PointerToRawData,sec[i].SizeOfRawData);
    return base;
}
static void Jump(DWORD from,void* to) {
    from=N(from);
    auto* p=reinterpret_cast<BYTE*>(from); p[0]=0xe9;
    const DWORD rel=reinterpret_cast<DWORD>(to)-from-5; memcpy(p+1,&rel,4);
    FlushInstructionCache(GetCurrentProcess(),p,5);
}
struct Item { DWORD padding[3]; int id; } pet={{},5000031};
struct Ref { DWORD ignored; Item* item; };
struct Packet { DWORD padding[2]; const BYTE* bytes; DWORD size,unknown,cursor; } packet;
static Item* slots[97];
static DWORD frame[128],*fp=frame+100;
static DWORD loopEntry=N(0xa1ec18),loopNext=N(0xa1f13e);
static int reads,adds,removes,finalized;
static int initialSlot=24;
static DWORD fakeMapNode[4];

static Ref* __fastcall Lookup(void*,void*,Ref* out,int type,int slot) {
    Require(type==5 && slot>0 && slot<97,"lookup type/slot decoded by native dispatcher");
    out->item=slots[slot]; return out;
}
static void __fastcall Release(Ref* ref,void*,int) { ref->item=nullptr; }
static int __fastcall ItemId(void* self,void*) { ++reads; return *static_cast<int*>(self); }
static int __fastcall Quantity(void*,void*,int) { return 0; }
static LONG WINAPI IncrementItemRef(volatile LONG* value) { return InterlockedIncrement(value); }
static DWORD* __fastcall MapSlot(void*,void*,int*,int) { return fakeMapNode; }
// Substitute item deserialization/backing storage only. The real EXE decodes
// operation count/mode/type/slot, dispatches both branches and advances its loop.
static Ref* __cdecl DecodeItem(Ref* out,Packet* p) {
    Require(p->cursor+5<=p->size && p->bytes[p->cursor++]==3,"next operation reaches pet decoder");
    int id=0; memcpy(&id,p->bytes+p->cursor,4); p->cursor+=4;
    Require(id==5000031,"pet payload remains aligned after empty removal");
    out->item=&pet; return out;
}
static void __fastcall SetSlot(void*,void*,int type,int slot,Ref item) {
    Require(type==5 && slot>0 && slot<97,"authoritative addition type/slot");
    slots[slot]=item.item; ++adds;
}
static void __cdecl ExistingRemoval() { slots[fp[-0x18/4]==5 ? 24:0]=nullptr; ++removes; }
__declspec(naked) static void FinishExistingRemoval() {
    __asm { call ExistingRemoval
        jmp dword ptr [loopNext] }
}
__declspec(naked) static void FinishAddition() { __asm { jmp dword ptr [loopNext] } }
__declspec(naked) static void FinishPacket() { __asm { inc finalized
        ret } }
__declspec(naked) static void InvokeInventory() {
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov ebp, fp
        xor ebx, ebx
        mov edi, initialSlot
        call dword ptr [loopEntry]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
static bool InventoryFaults() {
    __try { InvokeInventory(); }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
static void Reset(const BYTE* bytes,unsigned size) {
    memset(frame,0,sizeof(frame)); memset(slots,0,sizeof(slots));
    packet={ {},bytes,size,0,0 }; fp[2]=reinterpret_cast<DWORD>(&packet);
    reads=adds=removes=finalized=0;
}

static void* target;
static void* focus;
static DWORD focusSite=N(0x6016c8);
static int focusCalls;
static bool throwFocus;
static void __fastcall NativeFocusStub(void*,void*,void* value) {
    if (throwFocus) RaiseException(0xe0424242,0,0,nullptr);
    focus=value; ++focusCalls;
}
__declspec(naked) static void InvokeFocus() {
    __asm {
        push 1002
        push offset finished
        push esi
        push target
        xor ecx, ecx
        jmp dword ptr [focusSite]
    finished:
        ret
    }
}
static bool FocusFaults(DWORD code) {
    __try { InvokeFocus(); }
    __except(GetExceptionCode()==code ? EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
int main(int argc,char** argv) {
    Require(argc==2,"client path"); BYTE* base=Map(argv[1]);
    *reinterpret_cast<DWORD*>(N(0xa1ecc7))=N(0xbebf98);
    *reinterpret_cast<DWORD*>(N(0xa1f0ee))=N(0xaf01fc);
    Jump(0x4282f7,Lookup); Jump(0x428a50,Release);
    // Three operations: empty removal, authoritative pet addition, another
    // empty removal. This detects both early return and skipping too much.
    const BYTE refresh[]={3,3,5,24,0,0,5,24,0,3,0x5f,0x4b,0x4c,0,3,5,25,0};
    Reset(refresh,sizeof(refresh));
    Require(InventoryFaults(),"unpatched native empty removal reproduces access violation");
    const BYTE entry=base[0x61ecef]; base[0x61ecef]=0x90;
    Require(!InventoryRefreshFix::Install(reinterpret_cast<HMODULE>(base)),"reject incompatible inventory code");
    base[0x61ecef]=entry;
    Require(InventoryRefreshFix::Install(reinterpret_cast<HMODULE>(base)),"install inventory fix");
    Require(InventoryRefreshFix::Install(reinterpret_cast<HMODULE>(base)),"inventory install idempotent");
    Jump(0x4e33f9,DecodeItem); Jump(0x42873d,ItemId); Jump(0xa2858c,Quantity);
    Jump(0x7360e7,MapSlot); Jump(0x47b05a,SetSlot);
    Jump(0xa1ecfa,FinishExistingRemoval); Jump(0xa1f0fe,FinishAddition); Jump(0xa1f147,FinishPacket);
    *reinterpret_cast<void**>(N(0xaf01fc))=reinterpret_cast<void*>(&IncrementItemRef);
    Reset(refresh,sizeof(refresh)); InvokeInventory();
    Require(slots[24]==&pet && slots[25]==nullptr && adds==1 && removes==0,"empty removal retains authoritative addition");
    Require(packet.cursor==sizeof(refresh) && fp[-0x28/4]==0 && finalized==1,"all native packet operations and finalization reached");
    Reset(refresh,sizeof(refresh)); slots[24]=&pet; InvokeInventory();
    Require(removes==1 && adds==1 && slots[24]==&pet && finalized==1,"existing-item refresh retains original removal branch");
    Require(packet.cursor==sizeof(refresh),"existing-item packet alignment");
    // Enter the patched site directly for unsupported inventory/slot cases.
    // Restore the real getter, whose null+0xC access must continue to fault.
    const BYTE getter[]={0x55,0x8b,0xec,0x83,0xec,0x10}; memcpy(reinterpret_cast<void*>(N(0x42873d)),getter,sizeof(getter));
    loopEntry=N(0xa1ecef);
    for(int type=1;type<=4;++type) {
        Reset(refresh,sizeof(refresh)); fp[-0x18/4]=type;
        Require(InventoryFaults(),"other inventory failures are not suppressed");
    }
    Reset(refresh,sizeof(refresh)); fp[-0x18/4]=5;
    initialSlot=0;
    Require(InventoryFaults(),"nonpositive slot failure is not suppressed");
    initialSlot=-24;
    Require(InventoryFaults(),"negative slot failure is not suppressed");
    printf("PASS inventory: baseline AV, native multi-operation decode/dispatch/loop, existing removal, scope and version rejection\n");

    target=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);
    Require(target!=nullptr,"inaccessible stale-window test allocation");
    Require(FocusFaults(EXCEPTION_ACCESS_VIOLATION),"unpatched native focus reads dead window");
    const BYTE focusCall=base[0x2016c8]; base[0x2016c8]=0x90;
    Require(!CharacterSelectFocusFix::Install(reinterpret_cast<HMODULE>(base)),"reject incompatible focus call");
    base[0x2016c8]=focusCall;
    Require(CharacterSelectFocusFix::Install(reinterpret_cast<HMODULE>(base)),"install focus fix");
    Require(CharacterSelectFocusFix::Install(reinterpret_cast<HMODULE>(base)),"focus install idempotent");
    DWORD nodes[3][6]{};
    for(unsigned i=0;i<3;++i) { nodes[i][4]=0x70000000+i*0x100; nodes[i][1]=i==2 ? 0:reinterpret_cast<DWORD>(nodes[i+1]); }
    auto* list=reinterpret_cast<DWORD*>(N(0xbf1648)); list[2]=3; list[3]=reinterpret_cast<DWORD>(nodes[0]+4);
    focus=reinterpret_cast<void*>(0x12345678);
    InvokeFocus(); Require(focus==reinterpret_cast<void*>(0x12345678) && focusCalls==0,"unregistered unreadable target leaves current focus intact");
    Jump(0x9e3264,NativeFocusStub);
    for(unsigned i=0;i<3;++i) {
        target=reinterpret_cast<void*>(nodes[i][4]+4); InvokeFocus();
        Require(focus==target && focusCalls==i+1,"native registry finds first/middle/last registered window");
    }
    target=nullptr; InvokeFocus(); Require(focus==nullptr && focusCalls==4,"null focus retains native clearing behavior");
    target=reinterpret_cast<void*>(nodes[1][4]+4); nodes[0][1]=reinterpret_cast<DWORD>(nodes[2]);
    InvokeFocus(); Require(focus==nullptr && focusCalls==4,"window unregistered during callback is not refocused");
    list[3]=0; InvokeFocus(); Require(focusCalls==4,"empty registry is safe");
    list[3]=reinterpret_cast<DWORD>(nodes[0]+4); target=reinterpret_cast<void*>(nodes[0][4]+4); throwFocus=true;
    Require(FocusFaults(0xe0424242),"live-window focus errors propagate");
    printf("PASS focus: baseline AV, real native registry, unreadable/unregistered/live/null targets, propagation and version rejection\n");
}
