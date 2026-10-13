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
void CopyText(Snapshot& snapshot, DWORD address) {
    if (!address || snapshot.memoryCount==ARRAYSIZE(snapshot.memory)) return;
    auto& block=snapshot.memory[snapshot.memoryCount++]; block.address=address;
    // Copy only the bounded UTF-16 text, not adjacent heap contents. A short or
    // unreadable string retains its actual byte count in the existing ABI.
    for (unsigned i=0;i<sizeof(block.bytes);i+=2) {
        wchar_t c=0;
        if (address>MAXDWORD-i || !Read(address+i,&c,2)) break;
        memcpy(block.bytes+i,&c,2); block.copied+=2;
        if (!c) break;
    }
    block.size=sizeof(block.bytes);
}
DWORD KnownPcom() {
    const DWORD pc=reinterpret_cast<DWORD>(GetModuleHandleW(L"PCOM.dll"));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS32 nt{};
    return pc && Read(pc,&dos,sizeof(dos)) && dos.e_magic==IMAGE_DOS_SIGNATURE
        && dos.e_lfanew>=0x40 && dos.e_lfanew<=0x1000
        && Read(pc+dos.e_lfanew,&nt,sizeof(nt)) && nt.Signature==IMAGE_NT_SIGNATURE
        && nt.FileHeader.Machine==IMAGE_FILE_MACHINE_I386
        && nt.FileHeader.TimeDateStamp==0x4b7c140c && nt.OptionalHeader.SizeOfImage==0x25000 ? pc:0;
}
void CopyCashCategoryEvidence(Snapshot& snapshot) {
    const DWORD base=reinterpret_cast<DWORD>(clientBase);
    for (DWORD i=0;i<snapshot.frameCount;++i) {
        const auto& f=snapshot.frames[i];
        // 00468411 is the constructor's return from 00471F37. EBP-14 owns
        // CCashShop, EBP-10 is the commodity index; EBP-1C is already replaced
        // by 22000006, so derive the category only from the captured SN.
        if (f.words[1]!=base+0x68411 || f.address<0x60) continue;
        CopyBlock(snapshot,f.address-0x60,0x80);
        DWORD owner=0,index=0,context=0,rows=0,count=0,item=0,categories=0;
        Read(f.address-0x14,&owner,4); Read(f.address-0x10,&index,4);
        if (Read(base+0x7e7918,&context,4) && context && context<=MAXDWORD-0x36e4
            && Read(context+0x36e0,&rows,4) && rows>=4 && Read(rows-4,&count,4)
            && count<=50000 && index<count && rows<=MAXDWORD-index*8-8
            && Read(rows+index*8+4,&item,4)) CopyBlock(snapshot,item,0x50);
        if (!owner || owner>MAXDWORD-0x48) break;
        CopyBlock(snapshot,owner+0x44,4);
        if (!Read(owner+0x44,&categories,4) || categories<4) break;
        if (!Read(categories-4,&count,4) || count>1000) {
            CopyBlock(snapshot,categories-4,4); break;
        }
        // The deployed table has 30 rows (844 bytes with its count). It fits
        // four blocks; larger tables retain their count and a bounded prefix.
        const DWORD bytes=4+count*0x1c;
        for (DWORD offset=0;offset<bytes && snapshot.memoryCount<ARRAYSIZE(snapshot.memory);offset+=256) {
            if (categories-4>MAXDWORD-offset) break;
            const DWORD remaining=bytes-offset;
            CopyBlock(snapshot,categories-4+offset,remaining>256?256:remaining);
        }
        break;
    }
}
void CopyResourceEvidence(Snapshot& snapshot) {
    const DWORD base=reinterpret_cast<DWORD>(clientBase);
    if (snapshot.family==Family::PcomFactory) {
        const DWORD pc=KnownPcom();
        if (!pc) return;
        CopyBlock(snapshot,pc+0x1ca20,0xb0); // self handle, module path, factory caches
        CopyBlock(snapshot,static_cast<DWORD>(snapshot.record.ExceptionInformation[1]),16);
        for (DWORD i=0;i<snapshot.frameCount;++i) {
            const auto& f=snapshot.frames[i];
            if (f.words[1]!=pc+0x4e67 || f.address<0x48) continue;
            CopyBlock(snapshot,f.address-0x48,0x80); // factory locals before unwinding
            DWORD holder=0,text=0;
            // +50B2 reuses argument 1 for the normalized _bstr_t holder.
            if (Read(f.words[2],&text,4)) CopyText(snapshot,text);
            if (i+1<snapshot.frameCount) {
                const auto& caller=snapshot.frames[i+1];
                if (caller.address<0x24) break;
                CopyBlock(snapshot,caller.address-0x24,0x44);
                // PcCreateObject clears its original argument at +4D19, but
                // retains the copied original class in EBP-14 until unwinding.
                if (Read(caller.address-0x14,&holder,4) && Read(holder,&text,4)) CopyText(snapshot,text);
            }
            break;
        }
    } else if (snapshot.family==Family::BossResource) {
        CopyBlock(snapshot,base+0x7f14e0,16); // root, ResMan, graphics globals
        CopyBlock(snapshot,static_cast<DWORD>(snapshot.record.ExceptionInformation[1]),16);
        for (DWORD i=0;i<snapshot.frameCount;++i) {
            const auto& f=snapshot.frames[i];
            if (f.words[1]!=base+0x133315 || f.address<0x200) continue;
            // Include original function locals/return sites below the verified
            // packet caller. They are raw evidence, not assumed live COM values.
            CopyBlock(snapshot,f.address-0x200,256);
            CopyBlock(snapshot,f.address-0x100,256);
            CopyBlock(snapshot,f.address,32); // all five Boss arguments
            break;
        }
    } else if (snapshot.family==Family::CashCategory) CopyCashCategoryEvidence(snapshot);
}
void CaptureNow(EXCEPTION_POINTERS* info, Family family) {
    if (capturing || !captureSnapshot || !info || !info->ExceptionRecord || !info->ContextRecord) return;
    const DWORD error=GetLastError();
    const LONG bit=1L<<static_cast<unsigned>(family);
    if (InterlockedOr(&claimed,bit)&bit) {
        SetLastError(error);
        return;
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
    if (snapshot.record.ExceptionCode==EXCEPTION_ACCESS_VIOLATION) {
        const DWORD target=static_cast<DWORD>(snapshot.record.ExceptionInformation[1]);
        VirtualQuery(reinterpret_cast<void*>(target),&snapshot.faultRegion,sizeof(snapshot.faultRegion));
    }
    CopyBlock(snapshot,snapshot.context.Esp,256);
    const DWORD address=reinterpret_cast<DWORD>(snapshot.record.ExceptionAddress);
    // Cash needs four category blocks. Its throw site is already retained in
    // the record/frame chain; spend this slot on its live data instead of code.
    if (address>=16 && family!=Family::CashCategory) CopyBlock(snapshot,address-16,64);
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
    CopyResourceEvidence(snapshot);
    // A failing diagnostic writer must not replace the observed exception or
    // permanently hold the per-thread recursion guard.
    __try { captureSnapshot(snapshot); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    capturing=false;
    SetLastError(error);
}
LONG CALLBACK Observe(EXCEPTION_POINTERS* info) {
    if (capturing || !info || !info->ExceptionRecord || !info->ContextRecord) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD error=GetLastError();
    Family family;
    if (Classify(*info->ExceptionRecord,family)) CaptureNow(info,family);
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
    case Family::PcomFactory:return "pcom_factory_first_throw";
    case Family::BossResource:return "boss_resource_first_throw";
    case Family::CashCategory:return "cash_category_first_throw";
    default:return "unknown";
    }
}
void ObserveResourceCpp(EXCEPTION_POINTERS* info,DWORD hresult,void* const* frames,unsigned count) {
    if ((hresult!=0x800401f8 && hresult!=0x80004003)
        || !observer || capturing || !clientBase || !frames || !info || !info->ExceptionRecord || !info->ContextRecord
        || info->ExceptionRecord->ExceptionCode!=0xe06d7363 || info->ExceptionRecord->NumberParameters!=3
        || info->ExceptionRecord->ExceptionInformation[0]!=0x19930520) return;
    const DWORD error=GetLastError();
    const DWORD base=reinterpret_cast<DWORD>(clientBase);
    const DWORD pc=hresult==0x800401f8 ? KnownPcom():0;
    bool factoryThrow=false,factoryCaller=false,bossCaller=false;
    for (unsigned i=0;i<count && i<24;++i) {
        const DWORD address=reinterpret_cast<DWORD>(frames[i]);
        if (pc && address==pc+0x52cf) factoryThrow=true;
        if (pc && address==pc+0x4e67) factoryCaller=true;
        if (address==base+0x133315) bossCaller=true;
    }
    // Start from the original exception context too: stack walking inside the
    // VEH itself can stop at an optimized handler frame and lose native callers.
    DWORD frame=info->ContextRecord->Ebp;
    for (unsigned i=0;i<24;++i) {
        DWORD words[2]{};
        if (!Read(frame,words,sizeof(words))) break;
        if (pc && words[1]==pc+0x52cf) factoryThrow=true;
        if (pc && words[1]==pc+0x4e67) factoryCaller=true;
        if (words[1]==base+0x133315) bossCaller=true;
        if (words[0]<=frame || words[0]-frame>1024*1024) break;
        frame=words[0];
    }
    if (factoryThrow && factoryCaller) CaptureNow(info,Family::PcomFactory);
    else if (hresult==0x80004003 && bossCaller) CaptureNow(info,Family::BossResource);
    SetLastError(error);
}
void ObserveCashCategoryCpp(EXCEPTION_POINTERS* info,DWORD code) {
    if (code!=0x22000006 || !observer || capturing || !clientBase || !info
        || !info->ExceptionRecord || !info->ContextRecord
        || info->ExceptionRecord->ExceptionCode!=0xe06d7363 || info->ExceptionRecord->NumberParameters!=3
        || info->ExceptionRecord->ExceptionInformation[0]!=0x19930520) return;
    const DWORD error=GetLastError(),base=reinterpret_cast<DWORD>(clientBase);
    bool throwSite=false,constructor=false;
    DWORD frame=info->ContextRecord->Ebp;
    for (unsigned i=0;i<24;++i) {
        DWORD words[2]{};
        if (!Read(frame,words,sizeof(words))) break;
        if (words[1]==base+0x722c7) throwSite=true;
        if (words[1]==base+0x68411 && throwSite) constructor=true;
        if (words[0]<=frame || words[0]-frame>1024*1024) break;
        frame=words[0];
    }
    if (throwSite && constructor) CaptureNow(info,Family::CashCategory);
    SetLastError(error);
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
