#include <windows.h>
#include <DbgHelp.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <xmmintrin.h>
// Compile the production writer and observer here to inject the isolated EXE
// mapping without adding a test switch or alternate policy to the DLL.
#include "../ezorsia/CrashReporter.cpp"
#include "../ezorsia/TargetedCrashSnapshot.cpp"
static void Require(bool ok,const char* why) {
    if (!ok) { printf("FAIL %s error=%lu\n",why,GetLastError());exit(1); }
}
static BYTE* image;
static DWORD faultSite;
static unsigned captures;
static TargetedCrashSnapshot::Snapshot saved[5];
static bool writerFailure;
static DWORD variantFault,variantEntry,variantOutput[4];
__declspec(naked) static void Fault() {
    __asm { mov eax, faultSite
            cmp eax, variantFault
            jne other
            push 1
            push 0
            lea ecx, variantOutput
            call dword ptr [variantEntry]
            ret
    other:  xor eax,eax
            jmp dword ptr [faultSite] }
}
static LONG Filter(EXCEPTION_POINTERS* info) {
    Require(info->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION,"native exception propagated");
    Require(info->ContextRecord->Eip==faultSite,"observer did not change exception EIP");
    Require(GetLastError()==177,"observer preserves last error");
    return EXCEPTION_EXECUTE_HANDLER;
}
static void HandledFault() {
    SetLastError(177);
    __try { Fault(); }
    __except(Filter(GetExceptionInformation())) {}
}
static DWORD WINAPI Worker(void*) { HandledFault();return 0; }
static void OnCapture(const TargetedCrashSnapshot::Snapshot& snapshot) {
    const unsigned index=static_cast<unsigned>(snapshot.family);
    saved[index]=snapshot;++captures;
    Require(snapshot.context.Eip==faultSite,"first-chance context matches actual fault");
    Require(snapshot.memory[0].copied==256,"original stack captured before writer");
    Require(snapshot.memory[1].copied==64,"actual fault code captured");
    // A fault inside a failing writer must not recursively capture another family.
    DWORD original=faultSite;faultSite=reinterpret_cast<DWORD>(image)+0x26b5b1;
    HandledFault();faultSite=original;
    CaptureTargetedSnapshot(snapshot);
}
static BYTE* Map(const char* path) {
    FILE* f=nullptr;Require(!fopen_s(&f,path,"rb"),"read EXE");
    fseek(f,0,SEEK_END);std::vector<BYTE> raw(ftell(f));rewind(f);fread(raw.data(),1,raw.size(),f);fclose(f);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(raw.data());
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(raw.data()+dos->e_lfanew);
    auto* base=static_cast<BYTE*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Require(base!=nullptr,"map offline EXE");memcpy(base,raw.data(),nt->OptionalHeader.SizeOfHeaders);
    auto* sec=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
        memcpy(base+sec[i].VirtualAddress,raw.data()+sec[i].PointerToRawData,sec[i].SizeOfRawData);
    // Trigger real AVs at the production classifier's relative addresses.
    for(DWORD offset: {0x26b5b1u,0x1234u}) { base[offset]=0x8b;base[offset+1]=0; }
    variantEntry=reinterpret_cast<DWORD>(base)+0x10fdf;
    variantFault=reinterpret_cast<DWORD>(base)+0x10ff5;
    base[0x660a98]=0xc6;base[0x660a99]=0;base[0x660a9a]=1;
    return base;
}
static std::vector<BYTE> ReadFileBytes(const wchar_t* path) {
    FILE* f=nullptr;Require(!_wfopen_s(&f,path,L"rb"),"read artifact");
    fseek(f,0,SEEK_END);std::vector<BYTE> b(ftell(f));rewind(f);fread(b.data(),1,b.size(),f);fclose(f);return b;
}
static unsigned VerifyArtifacts(bool failed) {
    wchar_t pattern[MAX_PATH];swprintf_s(pattern,L"%s\\ijl15-crash-*_targeted-*.txt",ClientLog::Directory());
    WIN32_FIND_DATAW data{};HANDLE search=FindFirstFileW(pattern,&data);unsigned count=0;
    if(search==INVALID_HANDLE_VALUE)return 0;
    do {
        wchar_t path[MAX_PATH];swprintf_s(path,L"%s\\%s",ClientLog::Directory(),data.cFileName);
        auto raw=ReadFileBytes(path);std::string text(raw.begin(),raw.end());
        Require(text.find("reportType=targeted_first_chance")!=std::string::npos,"first-chance explicitly marked");
        Require(text.find("fatalStatus=unknown_first_chance")!=std::string::npos,"not misclassified as fatal");
        Require(text.find("crashTraceEnabled=false")!=std::string::npos,"works without trace");
        Require(text.find("firstChanceTimestamp=")!=std::string::npos,"original observation time retained");
        const bool dllSample=text.find("targetedFamily=canvas")!=std::string::npos || text.find("targetedFamily=kernel_write")!=std::string::npos;
        Require((text.find("faultModuleTimestamp=")!=std::string::npos)==dllSample,"build metadata for loaded DLL only");
        Require(text.find("memory[0]")==std::string::npos,"raw stack bytes not uploaded in text");
        if(text.find("targetedFamily=null_variant")!=std::string::npos)
            Require(text.find("directCallerReturn=")!=std::string::npos && text.find("directCallerModuleOffset=")!=std::string::npos,
                "frameless native variant direct caller is present in text");
        Require(text.find(failed?"dumpWritten=false":"dumpWritten=true")!=std::string::npos,"dump result explicit");
        if(!failed) {
            wcscpy_s(wcsrchr(path,L'.'),5,L".dmp");auto dump=ReadFileBytes(path);
            PMINIDUMP_DIRECTORY dir=nullptr;void* stream=nullptr;ULONG length=0;
            Require(MiniDumpReadDumpStream(dump.data(),TargetedCrashSnapshot::StreamType,&dir,&stream,&length)!=FALSE,"local user stream readable");
            Require(length==sizeof(TargetedCrashSnapshot::Snapshot),"stream ABI size");
            const auto* s=static_cast<TargetedCrashSnapshot::Snapshot*>(stream);const unsigned family=static_cast<unsigned>(s->family);
            Require(family<5 && !memcmp(s,&saved[family],sizeof(*s)),"DbgHelp preserves copied snapshot");
            Require(MiniDumpReadDumpStream(dump.data(),ExceptionStream,&dir,&stream,&length)!=FALSE,"exception stream exists");
            const auto* e=static_cast<MINIDUMP_EXCEPTION_STREAM*>(stream);
            const auto* context=reinterpret_cast<CONTEXT*>(dump.data()+e->ThreadContext.Rva);
            Require(context->Eip==s->context.Eip && context->Esp==s->context.Esp,"standard dump retains original context");
        }
        ++count;
    } while(FindNextFileW(search,&data));FindClose(search);return count;
}
static void VerifyActionDetails() {
    auto s=saved[static_cast<unsigned>(TargetedCrashSnapshot::Family::ActionJump)];
    const DWORD base=reinterpret_cast<DWORD>(image),sp=s.context.Esp;
    s.context.Ebp=sp+0xc44; // Native local-action frame is larger than ESP capture.
    DWORD stack[]={base+0x52edbc,base+0x551799,61};
    memcpy(s.memory[0].bytes,stack,sizeof(stack));
    s.frameCount=1;s.frames[0].address=s.context.Ebp;s.frames[0].words[1]=base+0x56951e;
    auto& locals=s.memory[s.memoryCount++];locals.address=s.context.Ebp-0x90;locals.size=locals.copied=0x90;
    const DWORD skill=1321003;memcpy(locals.bytes+0x80,&skill,4);
    for(unsigned test=0;test<4;++test) {
        auto current=s;
        // Replace the actual copied locals, if present, with the known fixture.
        for(DWORD i=0;i<current.memoryCount;++i)
            if(current.memory[i].address==locals.address) current.memory[i]=locals;
        auto* code=const_cast<BYTE*>(SnapshotBytes(current,base+0x52edb2,10));Require(code!=nullptr,"copied prologue");
        if(test==1) { DWORD delta=0;memcpy(&delta,code+6,4);delta-=0x100;memcpy(code+6,&delta,4); }
        if(test==2) code[5]=0x90;
        if(test==3) current.memory[0].copied=0;
        wchar_t path[MAX_PATH];swprintf_s(path,L"%s\\action-details-%u.txt",ClientLog::Directory(),test);
        HANDLE f=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,0,nullptr);
        Require(f!=INVALID_HANDLE_VALUE,"create action details fixture");WriteTargetedDetails(f,current);CloseHandle(f);
        auto raw=ReadFileBytes(path);std::string text(raw.begin(),raw.end());
        if(test==0) Require(text.find("actionPrologueCallMatchesBaseline=true")!=std::string::npos,"intact CALL recognized");
        if(test==1) Require(text.find("actionPrologueCallMatchesBaseline=false")!=std::string::npos,"0x100 displacement change detected");
        if(test==2) Require(text.find("unavailable_or_changed_opcode")!=std::string::npos,"changed opcode not decoded as CALL");
        if(test<3) Require(text.find("localActionSkillId=1321003")!=std::string::npos && text.find("actionIndex=61")!=std::string::npos,"anchored action and skill decoded");
        else Require(text.find("localActionSkillId=")==std::string::npos && text.find("localActionCallChain=unconfirmed")!=std::string::npos,"missing stack does not invent a caller or skill");
    }
}
int main(int argc,char** argv) {
    Require(argc==3,"arguments");SetErrorMode(SEM_NOGPFAULTERRORBOX);
    image=Map(argv[1]);ClientLog::Initialize();
    const bool disabled=!strcmp(argv[2],"disabled");writerFailure=!strcmp(argv[2],"writer-failure");
    CrashReporter::Install(!disabled,"mini",false);CrashReporter::EnableConditionalDump(false);
    if(disabled) {
        faultSite=reinterpret_cast<DWORD>(image)+0x10ff5;HandledFault();
        Require(!TargetedCrashSnapshot::observer,"dump preference disables observer");
        Require(VerifyArtifacts(false)==0,"disabled produces no snapshots");puts("PASS targeted disabled");return 0;
    }
    // Production installation is active. Swap only the test mapping/callback.
    TargetedCrashSnapshot::clientBase=image;TargetedCrashSnapshot::captureSnapshot=OnCapture;
    Require(TargetedCrashSnapshot::observer!=nullptr,"default observer installed without lifecycle setting");
    TargetedCrashSnapshot::Family family;
    const std::string exePath=argv[1];
    const std::string canvasPath=exePath.substr(0,exePath.find_last_of("/\\")+1)+"Canvas.dll";
    // Map metadata/code only; never execute a system or Canvas fault site.
    HMODULE canvas=LoadLibraryExA(canvasPath.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Require(canvas!=nullptr,"map Canvas without initialization");
    EXCEPTION_RECORD record{};record.ExceptionCode=EXCEPTION_ACCESS_VIOLATION;record.NumberParameters=2;
    const DWORD kernelAddress=reinterpret_cast<DWORD>(GetModuleHandleW(L"kernel32.dll"))+0x1eb0b;
    const DWORD canvasAddress=reinterpret_cast<DWORD>(canvas)+0xed28;
    record.ExceptionAddress=reinterpret_cast<void*>(kernelAddress);
    record.ExceptionInformation[0]=1;
    Require(TargetedCrashSnapshot::Classify(record,family) && family==TargetedCrashSnapshot::Family::KernelWrite,"kernel ASLR classification");
    record.ExceptionInformation[0]=0;Require(!TargetedCrashSnapshot::Classify(record,family),"wrong access excluded");
    record.ExceptionAddress=reinterpret_cast<void*>(canvasAddress);
    Require(TargetedCrashSnapshot::Classify(record,family) && family==TargetedCrashSnapshot::Family::Canvas,"Canvas ASLR classification");
    record.ExceptionInformation[0]=1;Require(!TargetedCrashSnapshot::Classify(record,family),"Canvas write excluded");
    wchar_t metadataPath[MAX_PATH];swprintf_s(metadataPath,L"%s\\module-build-check.txt",ClientLog::Directory());
    HANDLE metadata=CreateFileW(metadataPath,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,0,nullptr);
    Require(metadata!=INVALID_HANDLE_VALUE,"create module metadata check");
    WriteModuleInfo(metadata,"fault",reinterpret_cast<void*>(kernelAddress));CloseHandle(metadata);
    const auto metadataBytes=ReadFileBytes(metadataPath);
    const std::string metadataText(metadataBytes.begin(),metadataBytes.end());
    Require(metadataText.find("faultModuleTimestamp=")!=std::string::npos &&
        metadataText.find("faultModuleImageSize=")!=std::string::npos,"fault module build identified");
    // Fail the dump CreateFile without disabling text, exercising the real writer failure path.
    if(writerFailure) {
        wchar_t path[MAX_PATH];swprintf_s(path,L"%s\\ijl15-crash-%s_targeted-3-tid%lu-codeC0000005-addr%p.dmp",
            ClientLog::Directory(),ClientLog::SessionId(),GetCurrentThreadId(),image+0x10ff5);
        Require(CreateDirectoryW(path,nullptr)!=FALSE,"block only dump path");
    }
    faultSite=reinterpret_cast<DWORD>(image)+0x1234;HandledFault();Require(!captures,"unrelated exceptions ignored");
    faultSite=reinterpret_cast<DWORD>(image)+0x10ff5;
    const auto csr=_mm_getcsr();HandledFault();HandledFault();Require(_mm_getcsr()==csr,"floating state retained");
    Require(captures==1,"one capture per family");
    HANDLE workers[8];for(auto& w:workers)w=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);
    WaitForMultipleObjects(8,workers,TRUE,10000);for(auto w:workers)CloseHandle(w);
    Require(captures==1,"cross-thread duplicates bounded");
    if(!writerFailure) {
        faultSite=reinterpret_cast<DWORD>(image)+0x26b5b1;HandledFault();
        faultSite=reinterpret_cast<DWORD>(image)+0x660a98;HandledFault();
        Require(captures==3,"different families independently captured");
        // Supply records for DLL offsets without modifying or executing DLL code.
        // The three EXE families above use actual hardware AV dispatch.
        for(DWORD site: {canvasAddress,kernelAddress}) {
            CONTEXT context{};RtlCaptureContext(&context);context.Eip=site;
            record.ExceptionAddress=reinterpret_cast<void*>(site);
            record.ExceptionInformation[0]=site==kernelAddress ? 1 : 0;
            EXCEPTION_POINTERS info{&record,&context};faultSite=site;
            SetLastError(177);
            Require(TargetedCrashSnapshot::Observe(&info)==EXCEPTION_CONTINUE_SEARCH,"DLL sample continues search");
            Require(TargetedCrashSnapshot::Observe(&info)==EXCEPTION_CONTINUE_SEARCH,"DLL duplicate continues search");
        }
        Require(captures==5,"all five families limited to one sample each");
        VerifyActionDetails();
    }
    Require(VerifyArtifacts(writerFailure)==captures,"text/dump artifacts validated");
    printf("PASS targeted %s captures=%u; EXE hardware AV / DLL supplied records, propagation, dedup, recursion, local stream and context verified\n",argv[2],captures);
}
