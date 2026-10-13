#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "../ezorsia/TargetedCrashSnapshot.cpp"

using namespace TargetedCrashSnapshot;
static Snapshot captured{};
static unsigned calls=0;
static bool failWriter=false;
static void Require(bool ok,const char* why) {
    if (!ok) { printf("FAIL %s error=%lu\n",why,GetLastError()); exit(1); }
}
static void OnSnapshot(const Snapshot& s) {
    captured=s; ++calls;
    SetLastError(765);
    if (failWriter) RaiseException(0xe0000042,0,0,nullptr);
}
static const BYTE* Bytes(DWORD address,unsigned size) {
    for (DWORD i=0;i<captured.memoryCount;++i) {
        const auto& m=captured.memory[i];
        if (address>=m.address && address-m.address<=m.copied && size<=m.copied-(address-m.address))
            return m.bytes+address-m.address;
    }
    return nullptr;
}
static bool HasText(const wchar_t* text) {
    const unsigned size=(wcslen(text)+1)*2;
    for (DWORD i=0;i<captured.memoryCount;++i)
        if (captured.memory[i].copied>=size && !memcmp(captured.memory[i].bytes,text,size)) return true;
    return false;
}
static DWORD FactoryRoute(DWORD pc) {
    for (DWORD i=0;i<captured.frameCount;++i) if (captured.frames[i].words[1]==pc+0x4e67) {
        const auto p=Bytes(captured.frames[i].address-0x10,4);
        if (p) return *reinterpret_cast<const DWORD*>(p);
    }
    return MAXDWORD;
}
static LONG CALLBACK NativeThrow(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode!=0xe06d7363) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD error=GetLastError();
    DWORD object[2]{}; SIZE_T copied=0;
    if (ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(info->ExceptionRecord->ExceptionInformation[1]),
        object,sizeof(object),&copied) && copied==sizeof(object)) {
        void* frames[24]{}; const auto count=CaptureStackBackTrace(0,24,frames,nullptr);
        ObserveResourceCpp(info,object[1],frames,count);
        Require(GetLastError()==error,"first-chance observer preserves LastError");
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
int wmain(int argc,wchar_t** argv) {
    Require(argc==2,"original PCOM path");
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    // Metadata-only client fixture; no game entry, imports, network or window.
    auto image=static_cast<BYTE*>(VirtualAlloc(nullptr,0xa94000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Require(image!=nullptr,"fixture allocation");
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image); dos->e_magic=IMAGE_DOS_SIGNATURE; dos->e_lfanew=0x80;
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(image+0x80); nt->Signature=IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine=IMAGE_FILE_MACHINE_I386; nt->FileHeader.TimeDateStamp=0x4b7c15c9;
    nt->OptionalHeader.SizeOfImage=0xa94000;
    Require(Install(reinterpret_cast<HMODULE>(image),OnSnapshot),"observer enabled");
    HMODULE pc=LoadLibraryW(argv[1]); Require(pc!=nullptr && KnownPcom()==reinterpret_cast<DWORD>(pc),"original PCOM identity");
    const auto init=reinterpret_cast<HRESULT(__cdecl*)()>(GetProcAddress(pc,"PcInitModule"));
    using Factory=HRESULT(__cdecl*)(const wchar_t*,const GUID*,void**,void*);
    const auto create=reinterpret_cast<Factory>(GetProcAddress(pc,"PcCreateObject"));
    Require(init && create && SUCCEEDED(init()),"original PCOM initialization");
    AddVectoredExceptionHandler(1,NativeThrow);
    const GUID property={0x986515d9,0x0a0b,0x4929,{0x8b,0x4f,0x71,0x86,0x82,0x17,0x7b,0x92}};
    void* result=nullptr;
    HRESULT hr=create(L"MissingResourceEvidenceProbe#A",&property,&result,nullptr);
    Require(hr==HRESULT(0x800401f8) && !result,"original failure remains HRESULT 800401F8");
    Require(calls==1 && captured.family==Family::PcomFactory,"actual PCOM first throw captured");
    auto stored=Bytes(reinterpret_cast<DWORD>(pc)+0x1ca24,4);
    Require(stored && *reinterpret_cast<const DWORD*>(stored)==reinterpret_cast<DWORD>(pc),"self-module value retained");
    Require(HasText(L"MissingResourceEvidenceProbe#A"),"actual original class copied before unwind");
    Require(HasText(L"MISSINGRESOURCEEVIDENCEPROBE"),"actual normalized module key copied before unwind");
    Require(FactoryRoute(reinterpret_cast<DWORD>(pc))==1,"real LoadLibrary branch flag retained");
    create(L"MissingResourceEvidenceProbe#A",&property,&result,nullptr);
    Require(calls==1,"factory repeated failure deduplicated");
    printf("PASS native PCOM first throw: original class, normalized key, self handle, frames, propagation, dedup\n");

    // Scope/partial-memory/writer-failure tests use supplied Boss records only.
    BYTE frameBytes[0x240]{}; DWORD frame=reinterpret_cast<DWORD>(frameBytes+0x200);
    auto words=reinterpret_cast<DWORD*>(frame); words[1]=reinterpret_cast<DWORD>(image)+0x133315;
    words[2]=8810018; words[3]=3; words[4]=5; words[5]=303200012; words[6]=2090000000;
    CONTEXT context{}; RtlCaptureContext(&context); context.Ebp=frame;
    EXCEPTION_RECORD record{}; record.ExceptionCode=0xe06d7363; record.NumberParameters=3;
    record.ExceptionInformation[0]=0x19930520; record.ExceptionInformation[1]=1;
    record.ExceptionAddress=reinterpret_cast<void*>(context.Eip);
    EXCEPTION_POINTERS info{&record,&context}; void* frames[]={reinterpret_cast<void*>(words[1])};
    ObserveResourceCpp(&info,0x800401f8,frames,1); Require(calls==1,"same HRESULT without PCOM frames ignored");
    words[1]+=1; frames[0]=reinterpret_cast<void*>(words[1]);
    ObserveResourceCpp(&info,0x80004003,frames,1); Require(calls==1,"unrelated E_POINTER ignored");
    words[1]-=1; frames[0]=reinterpret_cast<void*>(words[1]);
    failWriter=true; SetLastError(177); ObserveResourceCpp(&info,0x80004003,frames,1);
    Require(calls==2 && !TargetedCrashSnapshot::capturing && GetLastError()==177,"writer fault isolated and guard released");
    Require(captured.family==Family::BossResource && captured.memoryCount<=8,"Boss family and fixed memory bound");
    const auto args=Bytes(frame,32);
    Require(args && !memcmp(args,words,32),"Boss raw argument frame survives diagnostic writer");
    ObserveResourceCpp(&info,0x80004003,frames,1); Require(calls==2,"Boss family at most once");
    printf("PASS Boss scope, partial reads, arguments, bounded capture, failing writer, unchanged error\n");
    // Reset only the disposable harness's capture budget for the second causal
    // branch. The product never resets a session's first-throw budget.
    TargetedCrashSnapshot::claimed &= ~(1L<<static_cast<unsigned>(Family::PcomFactory));
    TargetedCrashSnapshot::snapshots[static_cast<unsigned>(Family::PcomFactory)] = {};
    failWriter=false;
    auto selfHandle=reinterpret_cast<DWORD*>(reinterpret_cast<BYTE*>(pc)+0x1ca24);
    const DWORD original=*selfHandle; *selfHandle=0;
    hr=create(L"Property",&property,&result,nullptr); *selfHandle=original;
    Require(hr==HRESULT(0x800401f8) && !result && calls==3,"zero self-handle failure captured and propagated");
    stored=Bytes(reinterpret_cast<DWORD>(pc)+0x1ca24,4);
    Require(stored && !*reinterpret_cast<const DWORD*>(stored) && FactoryRoute(reinterpret_cast<DWORD>(pc))==0,
        "built-in branch distinguishes zero self handle from LoadLibrary failure");
    Require(HasText(L"Property") && HasText(L"PROPERTY"),"built-in class strings survive first throw");
    puts("PASS native PCOM built-in failure: Property/PROPERTY, self handle zero, route=0");
}
