#include "stdafx.h"
#include "ClientLog.h"
#include "ClientDiagnostics.h"
#include "DisconnectDiagnostics.h"
#include "CrashReporter.h"
#include "ProcessExitMonitor.h"
#include "ConditionalDumpPolicy.h"
#include "ResourceProvenance.h"
#include <cstring>
#include <DbgHelp.h>
DWORD WINAPI Worker(void*) {
 __try { RaiseException(EXCEPTION_INT_DIVIDE_BY_ZERO,0,0,nullptr); }
 __except(EXCEPTION_EXECUTE_HANDLER) { ClientLog::Append(ClientLog::Component::Lifecycle,"harness_exception_handled worker=1"); }
 return 0;
}
BOOL WINAPI FailDump(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,const MINIDUMP_EXCEPTION_INFORMATION*,const MINIDUMP_USER_STREAM_INFORMATION*,const MINIDUMP_CALLBACK_INFORMATION*) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
bool limitMode = false;
unsigned long long testFingerprint = 0;
LONG Conditional(EXCEPTION_POINTERS* info) {
 CrashReporter::CaptureConditionalException(info, "harness", limitMode ? ++testFingerprint : 0);
 return EXCEPTION_EXECUTE_HANDLER;
}
void GenericFault() {
 __try { RaiseException(0xE06D7363,0,0,nullptr); }
 __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void ConditionalFault() {
 __try { RaiseException(EXCEPTION_INT_DIVIDE_BY_ZERO,0,0,nullptr); }
 __except(Conditional(GetExceptionInformation())) {}
}

__declspec(naked) DWORD __stdcall ResourceStub(DWORD,DWORD,DWORD,DWORD) {
 __asm { mov eax, 12345678h
         ret 16 }
}

__declspec(naked) void QueueStub() {
 __asm { mov ecx, [ebp-10h]
         mov [edi], ecx
         ret }
}
void InvokeQueue(DWORD frame, DWORD object, DWORD* destination) {
 __asm {
    push ebp
    push esi
    push edi
    mov eax, frame
    mov esi, object
    mov edi, destination
    mov ebp, eax
    call ResourceProvenance::QueueHook
    pop edi
    pop esi
    pop ebp
 }
}
int main(int argc,char** argv) {
 const std::uintptr_t stack[] = {0xA60BEB,0x4FECA9,0x96CB8D};
 if (!ConditionalDumpPolicy::Matches(0xE06D7363,1000,2000,stack,3,0x400000)
     || ConditionalDumpPolicy::Matches(0xE06D7363,1000,6001,stack,3,0x400000)
     || ConditionalDumpPolicy::Matches(0xE06D7363,0,2000,stack,3,0x400000)
     || ConditionalDumpPolicy::Matches(0xC0000005,1000,2000,stack,3,0x400000)
     || ConditionalDumpPolicy::Matches(0xE06D7363,1000,2000,stack,1,0x400000)) return 92;
 const std::uintptr_t resourceStack[] = {0xA60BEB,0x403AF7,0x43E8D8,0x4444C4};
 if (!ConditionalDumpPolicy::ResourceFailure(0xE06D7363,resourceStack,4,0x400000)
     || ConditionalDumpPolicy::ResourceFailure(0xC0000005,resourceStack,4,0x400000)
     || ConditionalDumpPolicy::ResourceFailure(0xE06D7363,stack,3,0x400000)) return 93;
 SetErrorMode(SEM_NOGPFAULTERRORBOX);
 ClientLog::Initialize();
 bool disabled=argc>1&&!strcmp(argv[1],"disabled");
 DisconnectDiagnostics::Install(!disabled && !(argc>1&&(!strcmp(argv[1],"conditional")||!strcmp(argv[1],"limit"))));
 CrashReporter::Install(true,"mini",false);
 limitMode=argc>1&&!strcmp(argv[1],"limit");
 bool conditional=limitMode || (argc>1&&!strcmp(argv[1],"conditional"));
 bool generic=argc>1&&!strcmp(argv[1],"generic");
 CrashReporter::EnableConditionalDump(conditional || generic);
 if (generic) { for(int i=0;i<2;++i) GenericFault(); return 0; }
 if(disabled) return 0;
 if(argc>1&&!strcmp(argv[1],"provenance")) {
  wchar_t original[256]=L"Effect/Test.img/source", composed[256]=L"Effect/Test.img/source/1";
  DWORD input=reinterpret_cast<DWORD>(original), output=reinterpret_cast<DWORD>(composed);
  DWORD frame[24]{}; DWORD fp=reinterpret_cast<DWORD>(&frame[8]);
  frame[4]=reinterpret_cast<DWORD>(&output);frame[9]=0x12345678;
  frame[17]=reinterpret_cast<DWORD>(&input); // EBP+24
  ResourceProvenance::queueOriginal=reinterpret_cast<void*>(QueueStub);
  DWORD written=0;
  InvokeQueue(fp,0x11223344,&written);
  if(written!=reinterpret_cast<DWORD>(&output)) return 96;
  original[0]=L'X'; // Provenance owns a copy, not a native string pointer.
  ResourceProvenance::resourceOriginal=reinterpret_cast<void*>(ResourceStub);
  auto hook=reinterpret_cast<DWORD(__stdcall*)(DWORD,DWORD,DWORD,DWORD)>(ResourceProvenance::ResourceHook);
  SetLastError(177);
  DWORD result=hook(0,reinterpret_cast<DWORD>(&output),0,0);
  if(result!=0x12345678 || GetLastError()!=177) return 94;
  ResourceProvenance::Failure();
  wchar_t invalid[256]{};
  if(ResourceProvenance::Wide(1,invalid,256)) return 95;
  return 0;
 }
 if(argc>1&&!strncmp(argv[1],"monitor-",8)) {
  if(!ProcessExitMonitor::Start()) return 90;
  if(!strcmp(argv[1],"monitor-external")) {
   HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr); WaitForSingleObject(event,INFINITE);
  }
  if(!strcmp(argv[1],"monitor-crash")) *reinterpret_cast<volatile int*>(0)=123;
  if(!strcmp(argv[1],"monitor-terminate")) TerminateProcess(GetCurrentProcess(),37);
  return 23;
 }
 if(conditional) {
  SetUnhandledExceptionFilter(nullptr); // Native client replaces our final handler.
  for(int i=0;i<(limitMode ? 6 : 2);++i) ConditionalFault();
  return 0;
 }
 if(argc>1&&!strcmp(argv[1],"dumpfail")) {
  void* writer=reinterpret_cast<void*>(&MiniDumpWriteDump);
  if(!Memory::SetHook(true,&writer,FailDump)) return 91;
  *reinterpret_cast<volatile int*>(0)=123;
 }
 if(argc>1&&!strcmp(argv[1],"crash")) { *reinterpret_cast<volatile int*>(0)=123; }
 if(argc>1&&!strcmp(argv[1],"terminate")) { TerminateProcess(GetCurrentProcess(),37); }
 HANDLE t=CreateThread(nullptr,0,Worker,nullptr,0,nullptr);WaitForSingleObject(t,10000);CloseHandle(t);
 DisconnectDiagnostics::SkillUse(22161003,1);
 ExitProcess(23);
}
