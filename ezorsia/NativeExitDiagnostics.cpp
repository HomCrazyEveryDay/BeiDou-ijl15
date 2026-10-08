#include "stdafx.h"
#include "NativeExitDiagnostics.h"
#include "ClientDiagnostics.h"
#include "ClientLog.h"
#include "detours.h"
#include <intrin.h>
#include <cstring>

namespace NativeExitDiagnostics {
namespace {
using Run = void(__thiscall*)(void*, int*);
using PostQuit = void(WINAPI*)(int);
Run originalRun;
PostQuit originalPostQuit;
Capture captureException;
BYTE* clientBase;
PVOID observer;
bool installed;
volatile LONG exceptionReports=0, stopReports=0;
thread_local bool activeRun=false, recording=false;
struct CppEvent {
    ULONGLONG tick=0;
    char type[32]{};
    DWORD value=0;
    bool valueKnown=false;
    void* frames[12]{};
    USHORT frameCount=0;
};
thread_local CppEvent recent[8]{};
thread_local unsigned recentCount=0;
TargetedCrashSnapshot::Snapshot snapshots[4];

bool Read(ULONG_PTR address, void* output, SIZE_T size) {
    SIZE_T copied=0;
    return address && address<=MAXDWORD-size
        && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),output,size,&copied)
        && copied==size;
}
template<typename T> bool ReadValue(ULONG_PTR address,T& value) { return Read(address,&value,sizeof(value)); }

// x86 MSVC ThrowInfo -> CatchableTypeArray -> primary CatchableType -> RTTI.
// Read numeric payloads ONLY for verified types with known layouts. In particular
// CMSException's first DWORD is its error, not a vtable followed by an HRESULT.
void DecodeCpp(const EXCEPTION_RECORD& record,CppEvent& event) {
    strcpy_s(event.type,"unknown");
    if(record.ExceptionCode!=0xe06d7363 || record.NumberParameters!=3
        || record.ExceptionInformation[0]!=0x19930520) return;
    DWORD array=0,count=0,primary=0,type=0;
    const ULONG_PTR info=record.ExceptionInformation[2],object=record.ExceptionInformation[1];
    char name[48]{};
    if(!ReadValue(info+12,array) || !ReadValue(array,count) || !count || count>16
        || !ReadValue(array+4,primary) || !ReadValue(primary+4,type)
        || !Read(type+8,name,sizeof(name))) return;
    name[sizeof(name)-1]=0;
    const char* label=nullptr;unsigned offset=0;
    if(!strcmp(name,".?AVCDisconnectException@@")) label="CDisconnectException";
    else if(!strcmp(name,".?AVCTerminateException@@")) label="CTerminateException";
    else if(!strcmp(name,".?AVCPatchException@@")) label="CPatchException";
    else if(!strcmp(name,".?AVCMSException@@")) label="CMSException";
    else if(!strcmp(name,".?AVZException@@")) label="ZException";
    else if(!strcmp(name,".?AV_com_error@@")) { label="_com_error";offset=4; }
    if(!label) return;
    strcpy_s(event.type,label);
    event.valueKnown=ReadValue(object+offset,event.value);
}

LONG CALLBACK RememberCpp(EXCEPTION_POINTERS* info) {
    if(!activeRun || recording || !info || !info->ExceptionRecord
        || info->ExceptionRecord->ExceptionCode!=0xe06d7363) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD error=GetLastError();
    // An eight-entry rolling memory buffer survives native window-procedure
    // translation of a COM/resource error into CWvsApp's pending error field.
    // No disk I/O, raw payload, or dump on ordinary handled throws.
    CppEvent event{};
    event.tick=GetTickCount64();DecodeCpp(*info->ExceptionRecord,event);
    event.frameCount=CaptureStackBackTrace(1,ARRAYSIZE(event.frames),event.frames,nullptr);
    recent[recentCount++%ARRAYSIZE(recent)]=event;
    SetLastError(error);
    return EXCEPTION_CONTINUE_SEARCH;
}
void LogFrames(const char* source,const CppEvent& event) {
    ClientLog::Emergency("native_exit_cpp source=%s tick=%llu type=%s valueKnown=%d value=%08lX earlierNotNecessarilyCausal=1",
        source,event.tick,event.type,event.valueKnown,event.value);
    for(USHORT i=0;i<event.frameCount;++i) {
        MEMORY_BASIC_INFORMATION region{};VirtualQuery(event.frames[i],&region,sizeof(region));
        char path[MAX_PATH]{};
        if(region.AllocationBase) GetModuleFileNameA(static_cast<HMODULE>(region.AllocationBase),path,ARRAYSIZE(path));
        const char* name=strrchr(path,'\\');name=name ? name+1:path;
        ClientLog::Emergency("native_exit_frame source=%s index=%u module=%.64s offset=%08lX",
            source,i,name,static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(event.frames[i])-reinterpret_cast<ULONG_PTR>(region.AllocationBase)));
    }
}
void CopyBlock(TargetedCrashSnapshot::Snapshot& snapshot,DWORD address,DWORD size) {
    if(!address || snapshot.memoryCount==ARRAYSIZE(snapshot.memory)) return;
    auto& block=snapshot.memory[snapshot.memoryCount++];block.address=address;
    block.size=size>sizeof(block.bytes) ? sizeof(block.bytes):size;
    if(Read(address,block.bytes,block.size)) block.copied=block.size;
}
void InvokeCapture(const TargetedCrashSnapshot::Snapshot& snapshot,unsigned index) {
    // Writer failure must not replace the game's original exception.
    __try { captureException(snapshot,index); }
    __except(EXCEPTION_EXECUTE_HANDLER) { ClientLog::Emergency("native_exit_capture_failed originalExceptionPreserved=1"); }
}
LONG EscapingException(EXCEPTION_POINTERS* info,void* app) {
    if(recording || !info || !info->ExceptionRecord || !info->ContextRecord) return EXCEPTION_CONTINUE_SEARCH;
    if(info->ExceptionRecord->ExceptionCode==EXCEPTION_STACK_OVERFLOW) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD error=GetLastError();
    const LONG index=InterlockedIncrement(&exceptionReports)-1;
    if(index>=static_cast<LONG>(ARRAYSIZE(snapshots))) { SetLastError(error);return EXCEPTION_CONTINUE_SEARCH; }
    recording=true;
    auto& snapshot=snapshots[index];snapshot.family=TargetedCrashSnapshot::Family::MainLoopEscape;
    snapshot.threadId=GetCurrentThreadId();GetSystemTime(&snapshot.time);snapshot.tick=GetTickCount64();
    snapshot.record=*info->ExceptionRecord;snapshot.record.ExceptionRecord=nullptr;snapshot.context=*info->ContextRecord;
    CopyBlock(snapshot,snapshot.context.Esp,256);
    CopyBlock(snapshot,reinterpret_cast<DWORD>(app),0x54);
    if(snapshot.record.ExceptionCode==0xe06d7363 && snapshot.record.NumberParameters==3) {
        CopyBlock(snapshot,static_cast<DWORD>(snapshot.record.ExceptionInformation[1]),32);
        CopyBlock(snapshot,static_cast<DWORD>(snapshot.record.ExceptionInformation[2]),16);
    }
    DWORD frame=snapshot.context.Ebp;
    for(auto& entry:snapshot.frames) {
        entry.address=frame;if(!Read(frame,entry.words,sizeof(entry.words))) break;
        ++snapshot.frameCount;
        if(entry.words[0]<=frame || entry.words[0]-frame>1024*1024) break;
        frame=entry.words[0];
    }
    ClientDiagnostics::Snapshot identity;ClientDiagnostics::TrySnapshot(identity);
    {
        ClientLog::EmergencyBatch batch;
        ClientLog::Emergency("native_main_loop_escape report=%ld clientRunId=%s connectionId=%s code=%08lX address=%p fatalUnknown=1 beforeNativeCleanup=1",
            index+1,identity.clientRunId,identity.connectionId,snapshot.record.ExceptionCode,snapshot.record.ExceptionAddress);
        CppEvent current{};current.tick=snapshot.tick;DecodeCpp(snapshot.record,current);LogFrames("escaping",current);
        const unsigned count=recentCount>ARRAYSIZE(recent) ? ARRAYSIZE(recent):recentCount;
        for(unsigned i=0;i<count;++i) {
            const auto& event=recent[(recentCount-count+i)%ARRAYSIZE(recent)];
            if(snapshot.tick>=event.tick && snapshot.tick-event.tick<=10000) LogFrames("recent",event);
        }
    }
    InvokeCapture(snapshot,static_cast<unsigned>(index)+1);
    recording=false;SetLastError(error);
    return EXCEPTION_CONTINUE_SEARCH;
}
void LogStop(const char* reason,void* caller,int value) {
    if(InterlockedIncrement(&stopReports)>16) return;
    ClientDiagnostics::Snapshot identity;ClientDiagnostics::TrySnapshot(identity);
    ClientLog::Emergency("native_main_loop_stop reason=%s value=%d caller=%p clientRunId=%s connectionId=%s",
        reason,value,caller,identity.clientRunId,identity.connectionId);
}
void __fastcall RunObserved(void* app,void*,int* stop) {
    const bool previous=activeRun;
    activeRun=true;recentCount=0;
    __try {
        __try { originalRun(app,stop); }
        __except(EscapingException(GetExceptionInformation(),app)) {}
        const DWORD error=GetLastError();int value=-1;
        const bool readable=ReadValue(reinterpret_cast<ULONG_PTR>(stop),value);
        LogStop(!readable ? "unreadable_stop_flag":value ? "native_stop_flag":"normal_return_without_stop_flag",_ReturnAddress(),value);
        SetLastError(error);
    } __finally { activeRun=previous; }
}
void WINAPI PostQuitObserved(int code) {
    const DWORD error=GetLastError();
    if(activeRun && !recording) LogStop("post_quit_message",_ReturnAddress(),code);
    SetLastError(error);originalPostQuit(code);
}
}

bool Install(HMODULE client,Capture capture) {
    if(installed) return reinterpret_cast<BYTE*>(client)==clientBase;
    if(!capture) return false;
    auto* base=reinterpret_cast<BYTE*>(client);IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
    if(!Read(reinterpret_cast<ULONG_PTR>(base),&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE
        || dos.e_lfanew<0x40 || dos.e_lfanew>0x1000
        || !Read(reinterpret_cast<ULONG_PTR>(base)+dos.e_lfanew,&nt,sizeof(nt)) || nt.Signature!=IMAGE_NT_SIGNATURE
        || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 || nt.FileHeader.TimeDateStamp!=0x4b7c15c9
        || nt.OptionalHeader.SizeOfImage!=0xa94000) return false;
    const BYTE entry[]={0xb8,0x2c,0x7e,0xae,0,0xe8,0x3e,0xaf,6,0};
    const BYTE caller[]={0xe8,0x93,0x3f,0,0};
    const BYTE finish[]={0x83,0xbd,0x68,0xff,0xff,0xff,0x12,0x75,8,0x6a,0,0xff,0x15,0x1c,4,0xbf,0};
    if(memcmp(base+0x5f5c50,entry,sizeof(entry)) || memcmp(base+0x5f1cb8,caller,sizeof(caller))
        || memcmp(base+0x5f696e,finish,sizeof(finish))) return false;
    originalRun=reinterpret_cast<Run>(base+0x5f5c50);
    originalPostQuit=reinterpret_cast<PostQuit>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"PostQuitMessage"));
    if(!originalPostQuit) return false;
    observer=AddVectoredExceptionHandler(1,RememberCpp);if(!observer) return false;
    if(DetourTransactionBegin()!=NO_ERROR) { RemoveVectoredExceptionHandler(observer);observer=nullptr;return false; }
    LONG result=DetourUpdateThread(GetCurrentThread());
    if(result==NO_ERROR) result=DetourAttach(reinterpret_cast<void**>(&originalRun),RunObserved);
    if(result==NO_ERROR) result=DetourAttach(reinterpret_cast<void**>(&originalPostQuit),PostQuitObserved);
    if(result==NO_ERROR) result=DetourTransactionCommit();else DetourTransactionAbort();
    if(result!=NO_ERROR) { RemoveVectoredExceptionHandler(observer);observer=nullptr;return false; }
    clientBase=base;captureException=capture;installed=true;
    ClientLog::Emergency("native_exit_diagnostics_ready mainLoop=1 recentCppMemoryOnly=8 maxExceptionReports=4 maxStopEvents=16 independentOfVerboseLifecycle=1");
    return true;
}
}
