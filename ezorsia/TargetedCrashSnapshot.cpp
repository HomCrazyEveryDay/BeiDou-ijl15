#include "stdafx.h"
#include "TargetedCrashSnapshot.h"
#include <cstring>

namespace TargetedCrashSnapshot {
namespace {
BYTE* clientBase = nullptr;
Capture captureSnapshot = nullptr;
PVOID observer = nullptr;
volatile LONG claimed = 0;
thread_local bool capturing = false;
// One slot per family avoids allocations and keeps nested/concurrent faults
// from modifying the snapshot currently being written by DbgHelp.
Snapshot snapshots[static_cast<unsigned>(Family::Count)];

bool Read(DWORD address, void* output, SIZE_T size) {
    if (!address || address > MAXDWORD-size) return false;
    SIZE_T copied=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),output,size,&copied)
        && copied==size;
}
bool KnownClient(BYTE* base) {
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS32 nt{};
    return Read(reinterpret_cast<DWORD>(base),&dos,sizeof(dos)) && dos.e_magic==IMAGE_DOS_SIGNATURE
        && dos.e_lfanew>=0x40 && dos.e_lfanew<=0x1000
        && Read(reinterpret_cast<DWORD>(base)+dos.e_lfanew,&nt,sizeof(nt))
        && nt.Signature==IMAGE_NT_SIGNATURE && nt.FileHeader.Machine==IMAGE_FILE_MACHINE_I386
        && nt.FileHeader.TimeDateStamp==0x4B7C15C9 && nt.OptionalHeader.SizeOfImage==0xA94000;
}
bool AtModule(DWORD address, const wchar_t* name, DWORD offset) {
    const auto module=GetModuleHandleW(name);
    return module && address==reinterpret_cast<DWORD>(module)+offset;
}
bool Classify(const EXCEPTION_RECORD& record, Family& family) {
    if (record.ExceptionCode!=EXCEPTION_ACCESS_VIOLATION || record.NumberParameters<2) return false;
    const DWORD address=reinterpret_cast<DWORD>(record.ExceptionAddress);
    const DWORD base=reinterpret_cast<DWORD>(clientBase);
    const auto access=record.ExceptionInformation[0];
    if (base && address==base+0x660a98 && access==1) family=Family::ActionJump;
    else if (base && address==base+0x26b5b1 && access==0) family=Family::MobInitialization;
    else if (base && address==base+0x10ff5 && access==0) family=Family::NullVariant;
    else if (access==0 && AtModule(address,L"CANVAS.DLL",0xed28)) family=Family::Canvas;
    else if (access==1 && AtModule(address,L"KERNEL32.DLL",0x1eb0b)) family=Family::KernelWrite;
    else return false;
    return true;
}
void CopyBlock(Snapshot& snapshot, DWORD address, DWORD size) {
    if (snapshot.memoryCount==ARRAYSIZE(snapshot.memory) || !address) return;
    auto& block=snapshot.memory[snapshot.memoryCount++];
    block.address=address;
    block.size=size>sizeof(block.bytes) ? sizeof(block.bytes) : size;
    if (Read(address,block.bytes,block.size)) block.copied=block.size;
}
LONG CALLBACK Observe(EXCEPTION_POINTERS* info) {
    if (capturing || !info || !info->ExceptionRecord || !info->ContextRecord) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD error=GetLastError();
    Family family;
    if (!Classify(*info->ExceptionRecord,family)) {
        SetLastError(error);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const LONG bit=1L<<static_cast<unsigned>(family);
    if (InterlockedOr(&claimed,bit)&bit) {
        SetLastError(error);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    capturing=true;
    auto& snapshot=snapshots[static_cast<unsigned>(family)];
    snapshot.family=family;
    snapshot.threadId=GetCurrentThreadId();
    GetSystemTime(&snapshot.time);
    snapshot.tick=GetTickCount64();
    snapshot.record=*info->ExceptionRecord;
    snapshot.record.ExceptionRecord=nullptr; // Do not persist a live nested-record pointer.
    snapshot.context=*info->ContextRecord;
    const DWORD target=static_cast<DWORD>(snapshot.record.ExceptionInformation[1]);
    VirtualQuery(reinterpret_cast<void*>(target),&snapshot.faultRegion,sizeof(snapshot.faultRegion));
    CopyBlock(snapshot,snapshot.context.Esp,256);
    const DWORD address=reinterpret_cast<DWORD>(snapshot.record.ExceptionAddress);
    if (address>=16) CopyBlock(snapshot,address-16,64);
    if (family==Family::ActionJump) {
        const DWORD base=reinterpret_cast<DWORD>(clientBase);
        CopyBlock(snapshot,base+0x52edb2,16);
        CopyBlock(snapshot,base+0x660b90,48);
        // The local action handler keeps its skill/action in EBP locals;
        // its 0xC14-byte frame is outside the small ESP capture above.
        if (snapshot.context.Ebp>=0x90) CopyBlock(snapshot,snapshot.context.Ebp-0x90,0x90);
    } else if (family==Family::MobInitialization && snapshot.context.Ecx<=MAXDWORD-0x11c) {
        CopyBlock(snapshot,snapshot.context.Ecx,16);
        CopyBlock(snapshot,snapshot.context.Ecx+0x114,12);
    }
    DWORD frame=snapshot.context.Ebp;
    for (auto& output:snapshot.frames) {
        output.address=frame;
        if (!Read(frame,output.words,sizeof(output.words))) break;
        ++snapshot.frameCount;
        const DWORD next=output.words[0];
        if (next<=frame || next-frame>1024*1024) break;
        frame=next;
    }
    captureSnapshot(snapshot);
    capturing=false;
    SetLastError(error);
    // A first-chance sample may be caught by native code. Never handle it here.
    return EXCEPTION_CONTINUE_SEARCH;
}
}

const char* Name(Family family) {
    switch(family) {
    case Family::ActionJump:return "action_jump";
    case Family::MobInitialization:return "mob_initialization";
    case Family::NullVariant:return "null_variant";
    case Family::Canvas:return "canvas";
    case Family::KernelWrite:return "kernel_write";
    case Family::MainLoopEscape:return "main_loop_escape";
    default:return "unknown";
    }
}
bool Install(HMODULE client, Capture capture) {
    if (observer) return true;
    if (!capture) return false;
    auto* base=reinterpret_cast<BYTE*>(client);
    clientBase=KnownClient(base) ? base : nullptr;
    captureSnapshot=capture;
    observer=AddVectoredExceptionHandler(1,Observe);
    return observer!=nullptr;
}
}
