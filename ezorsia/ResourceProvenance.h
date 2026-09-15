#pragma once
#include <windows.h>
#include <cstring>
// 083 native resource wrapper and deferred-effect queue. Diagnostic only:
// fixed storage, no native object ownership, no resource substitution.
namespace ResourceProvenance {
struct Entry {
    ULONGLONG sequence=0, tick=0;
    DWORD thread=0, object=0, caller=0;
    DWORD args[10]{};
    void* stack[16]{};
    USHORT depth=0;
    bool pathComplete=false, inputComplete=false;
    wchar_t path[256]{}, input[256]{};
};
static Entry queue[256]{}, requests[32]{};
static unsigned queueNext=0, requestNext=0;
static ULONGLONG sequence=0;
static SRWLOCK lock=SRWLOCK_INIT;
static volatile LONG dropped=0;
static thread_local bool busy=false;
static void* resourceOriginal=reinterpret_cast<void*>(0x403A93);
static void* queueOriginal=reinterpret_cast<void*>(0x444000);
inline bool Read(DWORD address, void* out, SIZE_T size) {
    SIZE_T n=0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),out,size,&n) && n==size;
}
inline bool Wide(DWORD address, wchar_t* out, SIZE_T capacity) {
    if (!address) return false;
    MEMORY_BASIC_INFORMATION m{};
    if (!VirtualQuery(reinterpret_cast<void*>(address),&m,sizeof(m)) || m.State!=MEM_COMMIT) return false;
    const SIZE_T available=(reinterpret_cast<DWORD>(m.BaseAddress)+m.RegionSize-address)/2;
    const SIZE_T count=available<capacity-1 ? available : capacity-1;
    if(!count || !Read(address,out,count*2)) return false;
    for(SIZE_T i=0;i<count;++i) if(!out[i]) return true;
    out[count]=0;return false;
}
inline bool BstrData(DWORD data, wchar_t* out, SIZE_T capacity) {
    DWORD text=0; return Read(data,&text,4) && Wide(text,out,capacity);
}
inline void Store(Entry& entry, bool created) {
    if (!TryAcquireSRWLockExclusive(&lock)) { InterlockedIncrement(&dropped); return; }
    entry.sequence=++sequence;
    if(created) {queue[queueNext]=entry;queueNext=(queueNext+1)%ARRAYSIZE(queue);}
    else {requests[requestNext]=entry;requestNext=(requestNext+1)%ARRAYSIZE(requests);}
    ReleaseSRWLockExclusive(&lock);
}
inline void __stdcall ResourceEntry(DWORD originalStack) {
    if(busy) return; busy=true;
    const DWORD error=GetLastError();
    Entry e{}; e.tick=GetTickCount64(); e.thread=GetCurrentThreadId();
    DWORD args[5]{};
    if(Read(originalStack,args,sizeof(args))) {
        e.caller=args[0];
        e.pathComplete=BstrData(args[2],e.path,ARRAYSIZE(e.path));
        memcpy(e.args,args,sizeof(args));
        e.depth=CaptureStackBackTrace(0,ARRAYSIZE(e.stack),e.stack,nullptr);
        Store(e,false);
    }
    SetLastError(error); busy=false;
}
inline void __stdcall QueueEntry(DWORD object, DWORD frame) {
    if(busy) return; busy=true;
    const DWORD error=GetLastError();
    Entry e{};e.tick=GetTickCount64();e.thread=GetCurrentThreadId();e.object=object;
    Read(frame+4,&e.caller,4);Read(frame+8,e.args,sizeof(e.args));
    // EBP+24 is the original _bstr_t base path. EBP-10 contains the
    // fully composed path about to be assigned to node+18.
    DWORD composed=0;Read(frame-0x10,&composed,4);
    e.pathComplete=BstrData(composed,e.path,ARRAYSIZE(e.path));
    e.inputComplete=BstrData(e.args[7],e.input,ARRAYSIZE(e.input));
    e.depth=CaptureStackBackTrace(0,ARRAYSIZE(e.stack),e.stack,nullptr);
    Store(e,true);SetLastError(error);busy=false;
}
__declspec(naked) static void ResourceHook() {
    __asm {
        pushfd
        pushad
        mov ebx, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        lea eax, [ebx+36]
        push eax
        call ResourceEntry
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        jmp dword ptr [resourceOriginal]
    }
}
__declspec(naked) static void QueueHook() {
    __asm {
        pushfd
        pushad
        mov ebx, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        push dword ptr [ebx+8]
        push dword ptr [ebx+4]
        call QueueEntry
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        jmp dword ptr [queueOriginal]
    }
}
inline void Emit(const Entry& e,const char* kind) {
    ClientLog::Emergency("provenance kind=%s sequence=%llu tick=%llu thread=%lu object=%08lX caller=%08lX pathComplete=%d path=%ls inputComplete=%d input=%ls",
        kind,e.sequence,e.tick,e.thread,e.object,e.caller,e.pathComplete,e.path,e.inputComplete,e.input);
    ClientLog::Emergency("provenance_args sequence=%llu raw0=%08lX raw1=%08lX raw2=%08lX raw3=%08lX raw4=%08lX raw5=%08lX raw6=%08lX raw7=%08lX raw8=%08lX raw9=%08lX",
        e.sequence,e.args[0],e.args[1],e.args[2],e.args[3],e.args[4],e.args[5],e.args[6],e.args[7],e.args[8],e.args[9]);
    for(USHORT i=0;i<e.depth;++i) {
        MEMORY_BASIC_INFORMATION m{};VirtualQuery(e.stack[i],&m,sizeof(m));
        char module[MAX_PATH]{};GetModuleFileNameA(static_cast<HMODULE>(m.AllocationBase),module,MAX_PATH);
        const char* name=strrchr(module,'\\');name=name ? name+1 : module;
        ClientLog::Emergency("provenance_frame sequence=%llu frame=%u module=%s base=%p offset=%lX",e.sequence,i,name,m.AllocationBase,
            reinterpret_cast<DWORD>(e.stack[i])-reinterpret_cast<DWORD>(m.AllocationBase));
    }
}
inline void Failure() {
    const DWORD error=GetLastError();
    if(!TryAcquireSRWLockExclusive(&lock)) {ClientLog::Emergency("provenance unavailable=lock_busy");SetLastError(error);return;}
    const DWORD thread=GetCurrentThreadId();
    const Entry* latest=nullptr;
    for(unsigned i=0;i<ARRAYSIZE(requests);++i) if(requests[i].thread==thread && (!latest||requests[i].sequence>latest->sequence))latest=&requests[i];
    if(latest) {
        Emit(*latest,"resource_request");
        unsigned matches=0;
        for(unsigned i=0;i<ARRAYSIZE(queue);++i) if(queue[i].sequence && queue[i].sequence<latest->sequence &&
            queue[i].pathComplete && latest->pathComplete && !wcscmp(queue[i].path,latest->path)) {
            Emit(queue[i],"queued_path_candidate");++matches;
        }
        ClientLog::Emergency("provenance_summary sequence=%llu pathCandidates=%u correlation=path_not_proof dropped=%ld capacity=256",latest->sequence,matches,dropped);
    }else ClientLog::Emergency("provenance unavailable=no_thread_request dropped=%ld",dropped);
    ReleaseSRWLockExclusive(&lock);SetLastError(error);
}
inline void Install(bool verified) {
    if(!verified) {ClientLog::Emergency("provenance_install verified=0 enabled=0");return;}
    const BYTE resourceBytes[]={0xB8,0x94,0x53,0xA7,0x00};
    const BYTE queueBytes[]={0x8B,0x4D,0xF0,0x89,0x0F};
    if(memcmp(resourceOriginal,resourceBytes,5)||memcmp(queueOriginal,queueBytes,5)) {
        ClientLog::Emergency("provenance_install verified=0 reason=signature_mismatch");return;
    }
    bool a=Memory::SetHook(true,&resourceOriginal,ResourceHook);
    bool b=a && Memory::SetHook(true,&queueOriginal,QueueHook);
    if(a&&!b) Memory::SetHook(false,&resourceOriginal,ResourceHook);
    ClientLog::Emergency("provenance_install verified=1 resource=%d queue=%d capacity=256 requestCapacity=32",a&&b,b);
}
}
