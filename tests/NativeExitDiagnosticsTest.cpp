#include <windows.h>
#include <DbgHelp.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "../ezorsia/NativeExitDiagnostics.cpp"
#include "../ezorsia/CrashReporter.cpp"
#include "../ezorsia/TargetedCrashSnapshot.cpp"

static void Require(bool ok,const char* why) {
    if(!ok) { printf("FAIL %s win32=%lu\n",why,GetLastError());exit(1); }
}
class CDisconnectException { public: DWORD code; };
class CTerminateException { public: DWORD code; };
static bool unwound=false,writerFailure=false;
static int mode=0,captures=0,stop=0;
static BYTE application[0x100]{};
static TargetedCrashSnapshot::Snapshot captured[4];
static NativeExitDiagnostics::CppEvent cpp[4];
struct UnwindMarker { ~UnwindMarker() { unwound=true; } };

static void __fastcall LoopFixture(void*,void*,int*) {
    UnwindMarker marker;
    if(mode==0) {
        for(unsigned i=0;i<100;++i) {
            try { throw CDisconnectException{0x21000000+i}; }
            catch(const CDisconnectException&) {}
        }
        Require(captures==0,"locally caught exceptions do not produce reports");
        Require(NativeExitDiagnostics::recentCount==100,"bounded ring retains current-run exceptions in memory");
        PostQuitMessage(57);
        return;
    }
    if(mode==1) throw CDisconnectException{0x21000003};
    if(mode==2) *static_cast<volatile DWORD*>(nullptr)=1;
    if(mode==3) throw CTerminateException{0x22000002};
}
static void Capture(const TargetedCrashSnapshot::Snapshot& s,unsigned index) {
    Require(!unwound,"escaping exception captured before native stack unwinding");
    Require(index>=1 && index<=4,"report bounded index");
    captured[index-1]=s;++captures;
    Require(s.family==TargetedCrashSnapshot::Family::MainLoopEscape,"main-loop family");
    Require(s.memory[0].address==s.context.Esp && s.memory[0].copied==256,"original stack copied before writer");
    NativeExitDiagnostics::DecodeCpp(s.record,cpp[index-1]);
    if(writerFailure) RaiseException(0xe1239876,0,0,nullptr);
    CrashReporter::CaptureMainLoopException(s,index);
}
static void Invoke() {
    unwound=false;
    NativeExitDiagnostics::RunObserved(application,nullptr,&stop);
}
static bool AV() {
    __try { Invoke(); }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
static BYTE* Map(const char* path) {
    FILE* file=nullptr;Require(!fopen_s(&file,path,"rb"),"open EXE");
    fseek(file,0,SEEK_END);std::vector<BYTE> b(ftell(file));rewind(file);fread(b.data(),1,b.size(),file);fclose(file);
    const auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(b.data());
    const auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(b.data()+dos->e_lfanew);
    auto* image=static_cast<BYTE*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Require(image!=nullptr,"map EXE without launching");memcpy(image,b.data(),nt->OptionalHeader.SizeOfHeaders);
    const auto* section=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
        memcpy(image+section[i].VirtualAddress,b.data()+section[i].PointerToRawData,section[i].SizeOfRawData);
    return image;
}
static std::vector<BYTE> ReadFile(const wchar_t* path) {
    FILE* file=nullptr;Require(!_wfopen_s(&file,path,L"rb"),"read artifact");
    fseek(file,0,SEEK_END);std::vector<BYTE> b(ftell(file));rewind(file);fread(b.data(),1,b.size(),file);fclose(file);return b;
}
static std::string Logs() {
    wchar_t pattern[MAX_PATH];swprintf_s(pattern,L"%s\\ijl15-emergency-*.log",ClientLog::Directory());
    WIN32_FIND_DATAW data{};HANDLE find=FindFirstFileW(pattern,&data);Require(find!=INVALID_HANDLE_VALUE,"emergency log exists");
    wchar_t path[MAX_PATH];swprintf_s(path,L"%s\\%s",ClientLog::Directory(),data.cFileName);FindClose(find);
    auto bytes=ReadFile(path);return std::string(bytes.begin(),bytes.end());
}
static void VerifyArtifacts() {
    wchar_t pattern[MAX_PATH];swprintf_s(pattern,L"%s\\ijl15-crash-*_main-loop-*.txt",ClientLog::Directory());
    WIN32_FIND_DATAW data{};HANDLE find=FindFirstFileW(pattern,&data);Require(find!=INVALID_HANDLE_VALUE,"early text reports exist");unsigned count=0;
    do {
        wchar_t path[MAX_PATH];swprintf_s(path,L"%s\\%s",ClientLog::Directory(),data.cFileName);
        auto bytes=ReadFile(path);std::string text(bytes.begin(),bytes.end());
        Require(text.find("reportType=native_main_loop_escape")!=std::string::npos,"report identifies original loop escape");
        Require(text.find("targetedFamily=main_loop_escape")!=std::string::npos,"text family");
        Require(text.find("fatalStatus=unknown_first_chance")!=std::string::npos,"not mislabeled as guaranteed fatal");
        auto* extension=wcsrchr(path,L'.');wcscpy_s(extension,5,L".dmp");auto dump=ReadFile(path);
        PMINIDUMP_DIRECTORY directory=nullptr;void* stream=nullptr;ULONG size=0;
        Require(MiniDumpReadDumpStream(dump.data(),TargetedCrashSnapshot::StreamType,&directory,&stream,&size)!=FALSE,"original context custom stream exists");
        auto* snapshot=static_cast<TargetedCrashSnapshot::Snapshot*>(stream);
        Require(size==sizeof(*snapshot) && snapshot->family==TargetedCrashSnapshot::Family::MainLoopEscape,"custom stream layout");
        bool match=false;for(const auto& expected:captured) if(expected.tick==snapshot->tick && expected.record.ExceptionCode==snapshot->record.ExceptionCode
            && expected.context.Eip==snapshot->context.Eip && expected.context.Esp==snapshot->context.Esp) match=true;
        Require(match,"saved first-pass context preserved after dump writing");++count;
    } while(FindNextFileW(find,&data));FindClose(find);
    Require(count==3,"four captures with one writer failure produce three reports");
}
int main(int argc,char** argv) {
    Require(argc==2,"EXE path");ClientLog::Initialize();
    auto* image=Map(argv[1]);
    Require(!NativeExitDiagnostics::Install(nullptr,Capture),"reject missing EXE");
    const BYTE saved=image[0x5f5c50];image[0x5f5c50]=0x90;
    Require(!NativeExitDiagnostics::Install(reinterpret_cast<HMODULE>(image),Capture),"reject changed native loop");image[0x5f5c50]=saved;
    Require(NativeExitDiagnostics::Install(reinterpret_cast<HMODULE>(image),Capture),"install actual Detours against validated native entry");
    Require(image[0x5f5c50]==0xe9,"real native entry redirected");
    Require(NativeExitDiagnostics::Install(reinterpret_cast<HMODULE>(image),Capture),"idempotent");
    // Use real MSVC SEH/C++ unwinding and User32 message delivery. Substitute
    // only the application body; do not run the game/security/input loop.
    NativeExitDiagnostics::originalRun=reinterpret_cast<NativeExitDiagnostics::Run>(LoopFixture);
    CrashReporter::Install(true,"triage",false);CrashReporter::EnableConditionalDump(false);
    Invoke();Require(captures==0 && !NativeExitDiagnostics::activeRun,"handled throws + normal return preserved");
    MSG message{};Require(PeekMessageW(&message,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE) && message.wParam==57,"PostQuitMessage semantics preserved");
    auto text=Logs();Require(text.find("reason=post_quit_message")!=std::string::npos && text.find("native_main_loop_escape report=")==std::string::npos,"normal close distinguished without dump");
    mode=1;bool caught=false;
    try { Invoke(); } catch(const CDisconnectException& e) { caught=e.code==0x21000003; }
    Require(caught && unwound && captures==1 && !NativeExitDiagnostics::activeRun,"original C++ type/value reaches outer handler");
    Require(!strcmp(cpp[0].type,"CDisconnectException") && cpp[0].valueKnown && cpp[0].value==0x21000003,"native numeric error decoded at offset zero");
    writerFailure=true;caught=false;
    try { Invoke(); } catch(const CDisconnectException& e) { caught=e.code==0x21000003; }
    Require(caught && captures==2,"writer exception cannot replace original game exception");writerFailure=false;
    mode=2;Require(AV() && captures==3,"hardware AV captured and still reaches outer SEH handler");
    Require(captured[2].record.ExceptionAddress==reinterpret_cast<void*>(captured[2].context.Eip),"hardware fault context remains at true failing instruction");
    mode=3;caught=false;
    try { Invoke(); } catch(const CTerminateException& e) { caught=e.code==0x22000002; }
    Require(caught && captures==4 && !strcmp(cpp[3].type,"CTerminateException") && cpp[3].value==0x22000002,"terminate errors preserved separately");
    for(unsigned i=0;i<8;++i) {
        caught=false;try { Invoke(); } catch(const CTerminateException&) { caught=true; }
        Require(caught,"exceptions propagate beyond report budget");
    }
    Require(captures==4,"strict four-report budget");
    EXCEPTION_RECORD bad{};bad.ExceptionCode=0xe06d7363;bad.NumberParameters=3;bad.ExceptionInformation[0]=0x19930520;bad.ExceptionInformation[2]=0xfffffff0;
    NativeExitDiagnostics::CppEvent unknown{};NativeExitDiagnostics::DecodeCpp(bad,unknown);
    Require(!unknown.valueKnown && !strcmp(unknown.type,"unknown"),"invalid RTTI never guessed as HRESULT");
    VerifyArtifacts();text=Logs();
    Require(text.find("type=CDisconnectException valueKnown=1 value=21000003")!=std::string::npos,"uploadable logs retain root error without raw payload");
    Require(text.find("native_exit_capture_failed")!=std::string::npos,"writer failure is explicit");
    printf("PASS native exit: real C++/SEH propagation, pre-unwind context, handled exceptions memory-only, normal quit, RTTI/error decoding, writer failure, bounded artifacts\nArtifacts: %ls\n",ClientLog::Directory());
}
