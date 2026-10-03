#define NOMINMAX
#include <windows.h>
#include <cstring>
#include "Memory.h"
#include "detours.h"
namespace CrashReporter {void RecordEvent(const char*,const char*,...) {}}
bool Memory::SetHook(bool attach,void** target,void* detour) {
    if(DetourTransactionBegin()!=NO_ERROR)return false;
    if(DetourUpdateThread(GetCurrentThread())==NO_ERROR && (attach?DetourAttach:DetourDetach)(target,detour)==NO_ERROR && DetourTransactionCommit()==NO_ERROR)return true;
    DetourTransactionAbort();return false;
}
#include "MixedDyeUnderTest.cpp"
#include "MixedDyeEntryUnderTest.h"
#define MIXED_DYE_HOOK_TEST
#include "MixedDyeTest.cpp"
#ifdef MIXED_DYE_WINDOW_TEST
#include "MixedDyeWindowUnderTest.cpp"
#endif

static void MapClient() {
    FILE* file=nullptr;Require(_wfopen_s(&file,L"BeiDou.exe",L"rb")==0,"open supported EXE without executing");
    BYTE header[4096];Require(fread(header,1,sizeof(header),file)==sizeof(header),"PE header");
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(header+reinterpret_cast<IMAGE_DOS_HEADER*>(header)->e_lfanew);
    Require(nt->FileHeader.TimeDateStamp==0x4B7C15C9 && nt->OptionalHeader.SizeOfImage==0xA94000,"exact supported executable");
    auto base=static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(0x30400000),nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Require(base==reinterpret_cast<void*>(0x30400000),"isolated native image mapping");memcpy(base,header,sizeof(header));
    auto section=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){fseek(file,section[i].PointerToRawData,SEEK_SET);Require(fread(base+section[i].VirtualAddress,1,section[i].SizeOfRawData,file)==section[i].SizeOfRawData,"map section");}fclose(file);
}
static DWORD classify=0x305C94B8;
__declspec(naked) int Classification(int dummy,int id){
    __asm{
        push ebp
        mov ebp,esp
        call dword ptr[classify]
        pop ebp
        ret
    }
}
__declspec(naked) void UseCouponEntry(int position,int id){
    __asm{
        push ebp
        mov ebp,esp
        push esi
        call RedirectScriptedResetItemCave
        pop esi
        pop ebp
        ret
    }
}
static int allowedCalls=0,packets=0;
int __cdecl Allowed(int id){++allowedCalls;return id;}
struct Packet {int loopback;BYTE* data;DWORD size;unsigned offset;int encrypted;};
void __fastcall Capture(void* socket,void*,Packet* packet){
    Require(socket==reinterpret_cast<void*>(42) && packet->size==8 && packet->offset==0,"native cash packet ABI");
    Require(*reinterpret_cast<WORD*>(packet->data)==0x4f && *reinterpret_cast<WORD*>(packet->data+2)==7,"cash opcode and slot");
    Require(MixedDye::IsCoupon(*reinterpret_cast<int*>(packet->data+4)),"correct coupon ID");++packets;
}
void Jump(void* from,void* to){auto p=static_cast<BYTE*>(from);p[0]=0xe9;*reinterpret_cast<DWORD*>(p+1)=reinterpret_cast<DWORD>(to)-reinterpret_cast<DWORD>(from)-5;FlushInstructionCache(GetCurrentProcess(),p,5);}
#ifdef MIXED_DYE_WINDOW_TEST
#include "MixedDyeWindowChecks.h"
#endif
HRESULT __cdecl FailFactory(const wchar_t*,const GUID*,void**,void*){return E_OUTOFMEMORY;}
static int previewReleases=0;
void __fastcall PreviewRelease(void*,void*,int force){Require(force==0,"native preview reference cleanup ABI");++previewReleases;}
void CheckBeautySelection(){
    // Execute the real SetAvatar category branch and SetAvatarLook assignment.
    // Only stop after the resulting mode and isolate the native ZRef cleanup.
    *reinterpret_cast<BYTE*>(0x309ACAAF)=0xc3;
    Jump(reinterpret_cast<void*>(0x309AD356),PreviewRelease);
    using Category=int(__thiscall*)(void*);
    using Apply=void(__thiscall*)(void*,DWORD,void*);
    auto category=reinterpret_cast<Category>(0x309ACA8F);
    auto apply=reinterpret_cast<Apply>(0x309AA69E);
    for(int selected:{0,2,20000,21303,30000,30001,
        static_cast<int>(MixedDye::Encode(21303,2,true)),static_cast<int>(MixedDye::Encode(53086,1,true)),
        static_cast<int>(MixedDye::Encode(30000,1,false)),static_cast<int>(MixedDye::Encode(63800,1,false))}){
        int* list=&selected;const int mode=category(&list);
        MixedDye::Style style;const bool mixed=MixedDye::Decode(selected,style);
        const int expected=mixed?(style.face?0:1):(selected/10000==2?0:selected/10000==3?1:2);
        Require(mode==expected,"native beauty mode for ordinary and encoded styles");
        BYTE dialog[0x120]{},look[0x1c5]{},want[0x1c5]{};
        *reinterpret_cast<int**>(dialog+0x100)=list;
        *reinterpret_cast<int*>(dialog+0x104)=mode;
        *reinterpret_cast<int*>(look+0xD)=2;*reinterpret_cast<int*>(look+0x11)=20000;
        *reinterpret_cast<int*>(look+0x19)=64630;*reinterpret_cast<int*>(look+0x21)=1012764;
        std::memcpy(want,look,sizeof(want));
        *reinterpret_cast<int*>(want+(mode==0?0x11:mode==1?0x19:0xD))=selected;
        apply(dialog,0,look);
        Require(!std::memcmp(want,look,sizeof(want)),"preview changes only intended face/hair/skin field; preserves equipment");
    }
    Require(previewReleases==10,"all native selection calls complete cleanup");
    std::puts("PASS native beauty category and appearance assignment, including crash ID 0x50045337");
}
void CheckNativeFaceOrigin(void* rm){
    // Replay the actual v83 vector QueryInterface/assignment used at 409288.
    // The crash dump had origin=null, return address 4093FA, before any drawing.
    Require(*reinterpret_cast<DWORD*>(0x3041E4A2)==0x00BD8348,"native origin interface operand");
    *reinterpret_cast<DWORD*>(0x3041E4A2)=0x30BD8348;
    using Assign=HRESULT(__thiscall*)(void**,void**);
    auto assign=reinterpret_cast<Assign>(0x3041E48B);
    auto image=Load(rm,L"Character/Face/1342460727.img");
    unsigned frames=0;
    for(auto& expression:Names(image.p)){
        if(expression==L"info")continue;
        auto v=Resolve(image.p,expression);auto prop=Query(v.v,PropertyIID());if(!prop.p)continue;
        // default has a face canvas directly; expressions have numbered frames.
        for(auto& key:Names(prop.p)){
            auto entry=Resolve(image.p,expression+L"/"+key);auto frame=Query(entry.v,PropertyIID());
            Value canvasValue;
            if(frame.p)Get(frame.p,L"face",&canvasValue.v);else Check(VariantCopy(&canvasValue.v,&entry.v));
            auto canvas=Query(canvasValue.v,CanvasIID());if(!canvas.p)continue;
            auto meta=CanvasProperty(canvas.p);Value value;Get(meta.p,L"origin",&value.v);
            void* source=MixedDyeResources::Unknown(value.v);Object output;
            Require(SUCCEEDED(assign(&output.p,&source)) && output.p,"native face origin QI yields non-null");
            Require(Int(output.p,0x20)==Int(canvas.p,0x6c) && Int(output.p,0x28)==Int(canvas.p,0x74),"native compositor reads correct coordinates");++frames;
        }
    }
    Require(frames>=30,"actual failing face covers default and animated expressions");
    std::printf("PASS native face origin assignment for crash ID, %u frames\n",frames);
}
void CheckHook(void* rm,Factory factory){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* e)->LONG {
        MEMORY_BASIC_INFORMATION m{};VirtualQuery(e->ExceptionRecord->ExceptionAddress,&m,sizeof(m));wchar_t module[MAX_PATH]{};GetModuleFileNameW(static_cast<HMODULE>(m.AllocationBase),module,MAX_PATH);
        std::printf("EXCEPTION %08x at %p module offset=%zx\n",e->ExceptionRecord->ExceptionCode,e->ExceptionRecord->ExceptionAddress,reinterpret_cast<std::size_t>(e->ExceptionRecord->ExceptionAddress)-reinterpret_cast<std::size_t>(m.AllocationBase));std::wprintf(L"module %s\n",module);return EXCEPTION_EXECUTE_HANDLER;
    });
    MapClient();auto target=reinterpret_cast<BYTE*>(0x305C94B8);const BYTE first=*target;
    *target=0xcc;Require(!MixedDye::Install(),"mismatched executable rejected");*target=first;
    auto beautyTarget=reinterpret_cast<BYTE*>(0x309ACA93);const BYTE beautyFirst=*beautyTarget;
    *beautyTarget=0xcc;Require(!MixedDye::Install() && *target==first,"beauty signature mismatch leaves resource hook intact");*beautyTarget=beautyFirst;
    Require(MixedDye::Install(),"production patch signatures and installation");
    CheckBeautySelection();
    *reinterpret_cast<BYTE*>(0x305C94C3)=0xc3;
    for(int id:{30000,40590,63800,20000,53086,42150}){
        bool face=MixedDye::IsFace(id);int color=MixedDye::Color(id,face)==1?2:1;
        auto encoded=MixedDye::Encode(id,color,face);MixedDye::Style decoded;
        Require(MixedDye::Decode(encoded,decoded) && decoded.primary==id,"codec round trip");
        Require(Classification(0,encoded)==(face?2:3),"executed assembly uses correct resource category");
        Require(Classification(0,id)==id/10000,"ordinary classification unchanged");
    }
    MixedDye::nativeCashAllowed=Allowed;
    auto call=reinterpret_cast<BYTE*>(0x304EFEAD);
    auto allowed=reinterpret_cast<int(__cdecl*)(int)>(call+5+*reinterpret_cast<int*>(call+1));
    Require(allowed(5151040)==1 && allowed(5152302)==1 && allowedCalls==0,"new vouchers pass native double-click gate");
    Require(allowed(5151000)==5151000 && allowedCalls==1,"other cash items preserve predicate");
    *reinterpret_cast<void**>(0x30BE7914)=reinterpret_cast<void*>(42);
    Jump(reinterpret_cast<void*>(0x3049637B),Capture);
    MixedDye::SendCoupon(7,5151040);MixedDye::SendCoupon(7,5152302);MixedDye::SendCoupon(7,5151000);MixedDye::SendCoupon(0,5151040);
    Require(packets==2,"exact vouchers emit one correctly sized packet each");
    *reinterpret_cast<BYTE*>(0x30A0EAAC)=0xc3;
    UseCouponEntry(7,5151040);UseCouponEntry(7,5152302);
    Require(packets==4,"production cash dispatcher cave reaches coupon sender and restores stack");
    Require(MixedDye::AttachResourceManager(rm),"real PCOM GetObject detour installs");
    CheckNativeFaceOrigin(rm);
    for(int id:{30000,40590,63800,20000,53086}){
        bool face=MixedDye::IsFace(id);auto code=MixedDye::Encode(id,1,face);wchar_t encoded[100],base[100],second[100];
        swprintf_s(encoded,L"Character/%s/%08u.img",face?L"Face":L"Hair",code);
        swprintf_s(base,L"Character/%s/%08d.img",face?L"Face":L"Hair",id);
        swprintf_s(second,L"Character/%s/%08d.img",face?L"Face":L"Hair",MixedDye::WithColor(id,1,face));
        auto a=Load(rm,base),b=Load(rm,second),mixed=Load(rm,encoded),again=Load(rm,encoded);
        Require(mixed.p==again.p,"cached image identity reused");Require(ValidatePixels(a.p,b.p,mixed.p)>0,"detoured resources have real mixed pixels");
    }
    const auto count=MixedDye::Images().entries.size();MixedDye::factory=FailFactory;
    wchar_t failed[100];swprintf_s(failed,L"Character/Hair/%08u.img",MixedDye::Encode(40590,2,false));
    auto fallback=Load(rm,failed);Require(fallback.p && MixedDye::reports==1 && MixedDye::Images().entries.size()==count,"resource failure falls back without poisoned cache");
    MixedDye::factory=factory;
    auto held=Load(rm,L"Character/Hair/1073902896.img");
    int stress=0;
    for(int family:{30000,30020,30030})for(int color=0;color<8;++color)for(int other=0;other<8;++other){
        if(color==other)continue;wchar_t path[100];swprintf_s(path,L"Character/Hair/%08u.img",MixedDye::Encode(family+color,other,false));
        if(++stress%20==0)std::printf("cache stress %d\n",stress);
        auto mixed=Load(rm,path);Require(mixed.p!=nullptr,"cache stress image");
        Require(MixedDye::Images().entries.size()<=128 && MixedDye::Images().bytes<=32*1024*1024,"LRU count and pixel budget bounded");
    }
    Require(MixedDye::Images().entries.size()==128,"eviction exercised");
    Require(!Names(held.p).empty(),"caller-owned image survives cache eviction");
#ifdef MIXED_DYE_WINDOW_TEST
    CheckDedicatedWindow(rm,factory);
#endif
    Require(Memory::SetHook(false,reinterpret_cast<void**>(&MixedDye::originalGetObject),MixedDye::GetMixedObject),"resource detour removes cleanly");
    MixedDye::Images().entries.clear();MixedDye::Images().bytes=0;
    std::puts("PASS production assembly, double-click whitelist, packet ABI, real resource detour, bounded cache eviction and failure fallback");
}
