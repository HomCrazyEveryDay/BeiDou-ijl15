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
static int g_facePreviewFaceId=0,g_facePreviewFaceId2=0,g_facePreviewFaceId3=0;
#include "StyleClassificationUnderTest.h"
#include "PreviewClassificationUnderTest.h"
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
static DWORD inventoryQuantity=0x3081DEEE;
__declspec(naked) int NativeInventoryQuantity(void* window,void* item,int id) {
    __asm {
        push ebp
        mov ebp,esp
        sub esp,14h
        push ebx
        push esi
        mov ebx,window
        mov esi,item
        mov eax,id
        mov [ebp-14h],eax
        call dword ptr[inventoryQuantity]
        pop esi
        pop ebx
        mov esp,ebp
        pop ebp
        ret
    }
}
int __fastcall InventoryQuantity(void* item,void*) {return static_cast<int*>(item)[1];}
void PrepareInventoryCountProbe() {
    // Relocate only the native cash classifier's absolute jump-table operands.
    for(DWORD address:{0x3048642c,0x30486483,0x3048648a}) {
        auto operand=reinterpret_cast<DWORD*>(address);Require(*operand>=0x00486400 && *operand<0x00486900,"supported cash classifier tables");*operand+=0x30000000;
    }
    for(auto bounds:{std::make_pair(0x30486433u,0x3048645bu),std::make_pair(0x30486757u,0x30486807u)})
        for(DWORD address=bounds.first;address<bounds.second;address+=4)*reinterpret_cast<DWORD*>(address)+=0x30000000;
    // Stop after the real virtual GetQuantity call, before number drawing.
    *reinterpret_cast<BYTE*>(0x3081DF24)=0xc3;
    BYTE absent[]={0x33,0xc0,0xc3};std::memcpy(reinterpret_cast<void*>(0x3081DF5C),absent,sizeof(absent));
}
void CheckInventoryCount(bool patched) {
    BYTE window[0x620]{};*reinterpret_cast<int*>(window+0x5e4)=5;
    void* vtable[9]{};vtable[8]=reinterpret_cast<void*>(InventoryQuantity);
    DWORD item[2]={reinterpret_cast<DWORD>(vtable),0};
    using Predicate=int(__cdecl*)(int);
    auto oldAllowed=reinterpret_cast<Predicate>(0x304863D5),oldCash=reinterpret_cast<Predicate>(0x30486845);
    for(int id:{5151040,5152302,5151000,5152000,5152301,5152303,2000000,5000000,5072000})for(int count:{1,2,17,99}) {
        item[1]=count;
        const bool expected=(patched && MixedDye::IsCoupon(id)) || oldAllowed(id) || oldCash(id);
        Require(NativeInventoryQuantity(window,item,id)==(expected?count:0),"native inventory reads real quantity only for admitted item IDs");
        Require(item[1]==count,"quantity display never changes item data");
    }
    item[1]=17;
    Require(NativeInventoryQuantity(window,item,5152302)==(patched?17:0),"lens count bug reproduced before patch and fixed afterwards");
    *reinterpret_cast<int*>(window+0x5e4)=2;
    Require(NativeInventoryQuantity(window,item,2000000)==17,"ordinary consumable count remains native");
    std::printf("PASS native inventory count branch: patched=%d, counts 1/2/17/99, neighbors and consumables unchanged\n",patched);
}
__declspec(naked) int Classification(int dummy,int id){
    __asm{
        push ebp
        mov ebp,esp
        call dword ptr[classify]
        pop ebp
        ret
    }
}
__declspec(naked) int FaceResult(){__asm {mov eax,2} __asm {ret}}
__declspec(naked) int HairResult(){__asm {mov eax,3} __asm {ret}}
__declspec(naked) int OtherResult(){__asm {xor eax,eax} __asm {ret}}
__declspec(naked) int FinalStyleClassification(int category,int id){
    __asm {
        push ebp
        mov ebp,esp
        mov eax,category
        call faceHairCave
        pop ebp
        ret
    }
}
void CheckSpecialHairClassification() {
    faceRtn=reinterpret_cast<DWORD>(FaceResult);hairRtn=reinterpret_cast<DWORD>(HairResult);faceHairCaveRtn=reinterpret_cast<DWORD>(OtherResult);
    for(int id:{40902,40991,42150,42151,42152,42153,42154,42155,42156,42157,42160,42161,42162,42163,42164,42165,42166,42167}) {
        Require(FinalStyleClassification(id/10000,id)==3,"actual final cave routes special plain IDs to hair");
        Require(IsKnownHairId(id) && IsHighHairPreviewTarget(id) && !IsKnownFaceId(id) && !IsHighFacePreviewTarget(id),"production avatar preview agrees with resource classifier");
        auto encoded=MixedDye::Encode(id,(MixedDye::Color(id,false)+1)%8,false);
        Require(FinalStyleClassification(Classification(0,encoded),encoded)==3,"both production caves route encoded hair correctly");
        Require(IsKnownHairId(encoded) && IsHighHairPreviewTarget(encoded) && !IsKnownFaceId(encoded),"encoded avatar preview agrees with resource classifier");
    }
    for(int id:{20000,50000,80000})Require(FinalStyleClassification(id/10000,id)==2,"face categories retained");
    g_facePreviewFaceId=53086;Require(FinalStyleClassification(5,53086)==2,"temporary face preview retained");g_facePreviewFaceId=0;
    std::puts("PASS production final classification cave for 18 special hair IDs and encoded variants");
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
void CheckOrdinaryClimbingHair(void* rm) {
    for(int id:{40902,43206,44092}) {
        wchar_t path[96];swprintf_s(path,L"Character/Hair/%08d.img",id);
        auto hair=Load(rm,path),again=Load(rm,path);
        Require(hair.p==again.p,"ordinary repaired hair uses bounded cache");
        for(auto pose:{L"rope",L"ladder"})for(int frame:{0,1})for(auto layer:{L"backHair",L"backHairBelowCap"}) {
            auto child=std::wstring(pose)+L"/"+std::to_wstring(frame)+L"/"+layer;
            auto actual=Resolve(hair.p,child),source=Resolve(hair.p,L"backDefault/"+std::wstring(layer));
            CheckCanvasPixels(Query(source.v,CanvasIID()).p,Query(source.v,CanvasIID()).p,Query(actual.v,CanvasIID()).p);
        }
        swprintf_s(path,L"Character/Hair/%08d.img/rope/1",id);auto frame=Load(rm,path);
        Value actual;Get(frame.p,L"backHair",&actual.v);Require(Query(actual.v,CanvasIID()).p!=nullptr,"native suffix lookup also has the back hair");
        for(bool reversed:{false,true}) {
            auto other=MixedDye::WithColor(id,1,false);
            auto mixed=MixedDye::Encode(reversed?other:id,reversed?MixedDye::Color(id,false):1,false);
            swprintf_s(path,L"Character/Hair/%08u.img",mixed);auto image=Load(rm,path);
            swprintf_s(path,L"Character/Hair/%08d.img",other);auto second=Load(rm,path);
            for(auto pose:{L"rope",L"ladder"})for(int n:{0,1}) {
                auto child=std::wstring(pose)+L"/"+std::to_wstring(n)+L"/backHair";
                auto a=Resolve(hair.p,child),b=Resolve(second.p,child),m=Resolve(image.p,child);
                CheckCanvasPixels(Query(a.v,CanvasIID()).p,Query(b.v,CanvasIID()).p,Query(m.v,CanvasIID()).p);
            }
        }
        std::printf("PASS ordinary hair %d and both mixed orders: rope/ladder frames, back layers and suffix lookup\n",id);
    }
    std::uint32_t id=0;MixedDye::Style style;std::wstring suffix;
    for(auto path:{L"Character/Face/00043206.img",L"Character/Hair/00043200.img",L"Character/Hair/00043206.imgx",L"Character/Hair/42949672960.img"}) {
        auto key=SysAllocString(path);Require(!MixedDye::ParsePath(key,id,style,suffix),"unrelated or malformed plain resource paths are not intercepted");SysFreeString(key);
    }
}
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
#include "ChairImageLinksTest.h"
void CheckHook(void* rm,Factory factory){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* e)->LONG {
        MEMORY_BASIC_INFORMATION m{};VirtualQuery(e->ExceptionRecord->ExceptionAddress,&m,sizeof(m));wchar_t module[MAX_PATH]{};GetModuleFileNameW(static_cast<HMODULE>(m.AllocationBase),module,MAX_PATH);
        std::printf("EXCEPTION %08x at %p module offset=%zx\n",e->ExceptionRecord->ExceptionCode,e->ExceptionRecord->ExceptionAddress,reinterpret_cast<std::size_t>(e->ExceptionRecord->ExceptionAddress)-reinterpret_cast<std::size_t>(m.AllocationBase));std::wprintf(L"module %s\n",module);return EXCEPTION_EXECUTE_HANDLER;
    });
    MapClient();PrepareInventoryCountProbe();CheckInventoryCount(false);
    auto target=reinterpret_cast<BYTE*>(0x305C94B8);const BYTE first=*target;
    *target=0xcc;Require(!MixedDye::Install(),"mismatched executable rejected");*target=first;
    auto beautyTarget=reinterpret_cast<BYTE*>(0x309ACA93);const BYTE beautyFirst=*beautyTarget;
    *beautyTarget=0xcc;Require(!MixedDye::Install() && *target==first,"beauty signature mismatch leaves resource hook intact");*beautyTarget=beautyFirst;
    auto countTarget=reinterpret_cast<BYTE*>(0x3081DF06);const BYTE countFirst=*countTarget;
    *countTarget=0xcc;Require(!MixedDye::Install() && *target==first,"count signature mismatch leaves all hooks intact");*countTarget=countFirst;
    Require(MixedDye::Install(),"production patch signatures and installation");
    CheckInventoryCount(true);
    CheckBeautySelection();
    *reinterpret_cast<BYTE*>(0x305C94C3)=0xc3;
    CheckSpecialHairClassification();
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
    CheckChairImageLinks(rm,factory);
    CheckNativeFaceOrigin(rm);
    CheckOrdinaryClimbingHair(rm);
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
