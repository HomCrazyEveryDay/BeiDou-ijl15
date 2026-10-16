#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include "../ezorsia/MainPetInventoryMark.cpp"
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"oleaut32.lib")

namespace CrashReporter { void RecordEvent(const char*,const char*,...) {} }
static void Require(bool ok,const char* message) {
    if (!ok) { std::printf("FAIL %s\n",message); std::exit(1); }
}
static BYTE* MapClient(const char* path) {
    FILE* file=nullptr; Require(!fopen_s(&file,path,"rb"),"open client");
    fseek(file,0,SEEK_END); std::vector<BYTE> bytes(ftell(file)); rewind(file);
    Require(fread(bytes.data(),1,bytes.size(),file)==bytes.size(),"read client"); fclose(file);
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes.data()+dos->e_lfanew);
    auto base=static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(0x30400000),
        nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Require(base==reinterpret_cast<BYTE*>(0x30400000),"isolated EXE mapping; no game entry point");
    memcpy(base,bytes.data(),nt->OptionalHeader.SizeOfHeaders);
    auto section=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
        memcpy(base+section[i].VirtualAddress,bytes.data()+section[i].PointerToRawData,section[i].SizeOfRawData);
    return base;
}
static void Jump(BYTE* site,void* target) {
    site[0]=0xe9; const DWORD distance=reinterpret_cast<DWORD>(target)-reinterpret_cast<DWORD>(site)-5;
    memcpy(site+1,&distance,4); FlushInstructionCache(GetCurrentProcess(),site,5);
}
struct FakeCom { void** methods; ULONG references=1; };
static void* canvasMethods[33]{},*managerMethods[8]{},*itemMethods[7]{};
static FakeCom mark{canvasMethods},destination{canvasMethods},manager{managerMethods};
static int copies=0,loads=0,updates=0,invalidations=0,itemType=3;
static bool failLoad=false;
static int expectedX=6,expectedY=44;
static ULONG __stdcall AddRef(void* self) { return ++static_cast<FakeCom*>(self)->references; }
static ULONG __stdcall ReleaseRef(void* self) { return --static_cast<FakeCom*>(self)->references; }
static HRESULT __stdcall Query(void* self,REFIID,void** out) { *out=self; AddRef(self); return S_OK; }
static HRESULT __stdcall Origin(void*,LONG* out) { *out=2; return S_OK; }
static HRESULT __stdcall Width(void*,LONG* out) { *out=34; return S_OK; }
static HRESULT __stdcall Height(void*,LONG* out) { *out=38; return S_OK; }
static HRESULT __stdcall CopyMark(void* self,int x,int y,void* source,VARIANT alpha) {
    Require(self==&destination && source==&mark,"copy actual mark to inventory canvas");
    Require(x==expectedX && y==expectedY,"slot coordinates and canvas origin");
    Require(alpha.vt==VT_I4 && alpha.lVal==255,"alpha blend preserves existing pet/background");
    ++copies; return S_OK;
}
static HRESULT __stdcall GetMark(void*,BSTR path,VARIANT a,VARIANT b,VARIANT* out) {
    ++loads;
    Require(!wcscmp(path,L"UI/UIWindow.img/Item/bossPetIcon"),"original 083/CMS186/CMS228 resource path");
    Require(a.vt==VT_ERROR && b.vt==VT_ERROR,"native optional resource arguments");
    if(failLoad) return E_FAIL;
    out->vt=VT_UNKNOWN; out->punkVal=reinterpret_cast<IUnknown*>(&mark); AddRef(&mark); return S_OK;
}
static int __fastcall Type(void*,void*) { return itemType; }
static void __fastcall NativeUpdate(void*,void*) { ++updates; }
static void __fastcall InvalidateWindow(void*,void*,const RECT* rect) { Require(!rect,"redraw complete inventory"); ++invalidations; }
static BYTE window[0x620]{},character[0x40]{},pet[0x80]{},context[0x20bc]{};
static DWORD frame[80]{},*framePointer=frame+60,entry=0;
static DWORD observed[8]{};
__declspec(naked) static void FinishDraw() {
    __asm {
        mov observed[0],eax
        mov observed[4],ebx
        mov observed[8],ecx
        mov observed[12],edx
        mov observed[16],esi
        mov observed[20],edi
        pushfd
        pop observed[24]
        ret
    }
}
__declspec(naked) static void InvokeDraw() {
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov ebp,framePointer
        lea ebx,window
        lea esi,pet
        lea edi,character
        mov ecx,12345678h
        mov edx,23456789h
        push 246h
        popfd
        call dword ptr [entry]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
static void SetSerial(void* object,size_t offset,ULONGLONG value) { memcpy(static_cast<BYTE*>(object)+offset,&value,8); }
static void Draw(int expectedCopies) {
    int before=copies; InvokeDraw(); Require(copies==before+expectedCopies,"mark eligibility through real patched bridge");
    Require(mark.references==1,"release all temporary COM references");
    Require(observed[0]==*reinterpret_cast<DWORD*>(window+0x5e4),"replay displaced inventory-type read");
    Require(observed[1]==reinterpret_cast<DWORD>(window) && observed[2]==0x12345678 && observed[3]==0x23456789
        && observed[4]==reinterpret_cast<DWORD>(pet) && observed[5]==reinterpret_cast<DWORD>(character),"preserve live registers");
    Require((observed[6]&0x8d5)==(0x246&0x8d5),"preserve arithmetic flags");
}

static void CheckRealCanvas(const char* clientPath) {
    using DreamCanvas::Method;
    std::string directory(clientPath); directory.resize(directory.find_last_of("\\/"));
    Require(SetCurrentDirectoryA(directory.c_str())!=FALSE,"client resource directory");
    Require(SUCCEEDED(CoInitialize(nullptr)),"initialize COM");
    auto pcom=LoadLibraryA((directory+"/PCOM.dll").c_str());Require(pcom!=nullptr,"load native PCOM only");
    auto init=reinterpret_cast<HRESULT(__cdecl*)()>(GetProcAddress(pcom,"PcInitModule"));
    auto create=reinterpret_cast<DreamCanvas::Factory>(GetProcAddress(pcom,"PcCreateObject"));
    auto root=reinterpret_cast<HRESULT(__cdecl*)(void**,int)>(GetProcAddress(pcom,"PcRootNameSpace"));
    Require(init && create && root && SUCCEEDED(init()),"native PCOM factory");
    const GUID rmId={0x57dfe40b,0x3e20,0x4dbc,{0x97,0xe8,0x80,0x5a,0x50,0xf3,0x81,0xbf}};
    const GUID nsId={0x2aeeeb36,0xa4e1,0x4e2b,{0x8f,0x6f,0x2e,0x7b,0xde,0xc5,0xc5,0x3d}};
    const GUID fsId={0x352d8655,0x51e4,0x4668,{0x8c,0xe4,0x08,0x66,0xe2,0xb6,0xa5,0xb5}};
    const GUID canvasId={0x7600dc6c,0x9328,0x4bff,{0x96,0x24,0x5b,0x0f,0x5c,0x01,0x17,0x9e}};
    void *rm=nullptr,*ns=nullptr,*fs=nullptr;
    Require(SUCCEEDED(create(L"ResMan",&rmId,&rm,nullptr)) && rm,"native ResMan");
    Require(SUCCEEDED(create(L"NameSpace",&nsId,&ns,nullptr)) && ns,"native namespace");
    Require(SUCCEEDED(create(L"NameSpace#FileSystem",&fsId,&fs,nullptr)) && fs,"native filesystem");
    Require(SUCCEEDED(Method<HRESULT(__stdcall*)(void*,int,int,int)>(rm,0x14)(rm,0x11,-1,-1)),"loose IMG mode");
    Require(SUCCEEDED(root(&ns,1)),"native resource root");
    BSTR path=SysAllocString(L"Data");
    Require(SUCCEEDED(Method<HRESULT(__stdcall*)(void*,BSTR)>(fs,0x34)(fs,path)),"native Data mount");
    SysFreeString(path);path=SysAllocString(L"/");
    Require(SUCCEEDED(Method<HRESULT(__stdcall*)(void*,BSTR,void*,int)>(ns,0x18)(ns,path,fs,0)),"native namespace mount");
    SysFreeString(path);*resourceManager=rm;
    {
        CanvasRef original{LoadMark()},render;
        Require(original.value!=nullptr,"production loader reads shipped bossPetIcon");
        Require(SUCCEEDED(create(L"Canvas",&canvasId,&render.value,nullptr)),"offscreen canvas");
        VARIANT zero{},format{},alpha{};zero.vt=VT_I4;format.vt=VT_I4;format.lVal=2;alpha.vt=VT_I4;alpha.lVal=255;
        Require(SUCCEEDED(Method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(render.value,0x2c)(render.value,64,64,zero,format)),"create offscreen pixels");
        using Rect=HRESULT(__stdcall*)(void*,int,int,int,int,unsigned);
        Require(SUCCEEDED(Method<Rect>(render.value,0x8c)(render.value,0,0,64,64,0xffeeeeee)),"opaque background");
        auto pixel=[](void* canvas,int x,int y) {
            unsigned value=0;Require(SUCCEEDED(Method<HRESULT(__stdcall*)(void*,int,int,unsigned*)>(canvas,0x88)(canvas,x,y,&value)),"native canvas pixel");return value;
        };
        const unsigned background=pixel(render.value,0,0);
        SetSerial(character,0x1b,43);SetSerial(pet,0x18,43);
        DrawMark(window,character,pet,render.value,16,48);
        unsigned opaque=0,transparent=0;
        for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
            unsigned source=0;
            if(x>=14 && x<48 && y>=10 && y<48)source=pixel(original.value,x-14,y-10);
            const auto actual=pixel(render.value,x,y);
            if(!(source>>24)) {Require(actual==background,"transparent crown pixels preserve underlying image");++transparent;}
            if((source>>24)==255) {Require(actual==source,"original gold frame/crown pixels at correct position");++opaque;}
        }
        Require(opaque>50 && transparent>3000,"nonempty crown and transparent interior");
        for(int i=0;i<100;++i)DrawMark(window,character,pet,render.value,16,48);
    }
    *resourceManager=nullptr;DreamCanvas::Release(rm);DreamCanvas::Release(fs);
    void* empty=nullptr;root(&empty,1);DreamCanvas::Release(ns);
    auto terminate=reinterpret_cast<void(__cdecl*)()>(GetProcAddress(pcom,"PcTermModule"));
    Require(terminate!=nullptr,"PCOM shutdown");terminate();CoUninitialize();
    std::puts("PASS real PCOM/Canvas/ResMan load, original crown pixel placement, transparent compositing, repeated drawing");
}
int main(int argc,char** argv) {
    Require(argc==2,"client path"); auto base=MapClient(argv[1]); auto module=reinterpret_cast<HMODULE>(base);
    Require(!MainPetInventoryMark::Install(nullptr),"reject null module");
    for(DWORD rva:{0x41deee,0x41e2c1,0xe36c5,0xe3006,0x41de9b}) {
        base[rva]^=1;
        Require(!MainPetInventoryMark::Install(module),"reject mismatched native signature");
        base[rva]^=1;
        Require(base[0x41deee]==0x8b && base[0x41e2c1]==0xe8,"no partial install");
    }
    canvasMethods[0]=Query;canvasMethods[1]=AddRef;canvasMethods[2]=ReleaseRef;
    canvasMethods[0x6c/4]=Origin;canvasMethods[0x74/4]=Origin;canvasMethods[0x80/4]=CopyMark;
    canvasMethods[0x40/4]=Width;canvasMethods[0x48/4]=Height;
    managerMethods[0x1c/4]=GetMark; itemMethods[0x18/4]=Type;
    *reinterpret_cast<void***>(pet)=itemMethods;
    *reinterpret_cast<int*>(window+0x5e4)=5;
    SetSerial(character,0x1b,0x123456780000002aULL); SetSerial(pet,0x18,0x123456780000002aULL);
    framePointer[-0x10/4]=reinterpret_cast<DWORD>(&destination);framePointer[-0x40/4]=8;framePointer[-0x34/4]=82;
    entry=reinterpret_cast<DWORD>(base+0x41deee);Jump(base+0x41def4,FinishDraw);
    Draw(0); // Real original CUIItem path does not draw the existing resource.
    Require(MainPetInventoryMark::Install(module),"install actual draw/update patches");
    Require(MainPetInventoryMark::Install(module),"idempotent install");
    *resourceManager=&manager;*gameContext=context;*reinterpret_cast<void**>(context+0x20b8)=character;
    Jump(base+0x5e067e,NativeUpdate);Jump(base+0x5e04c9,InvalidateWindow);
    Draw(1);
    SetSerial(pet,0x18,0x223456780000002aULL);Draw(0); // Same low 32 bits must not match.
    SetSerial(pet,0x18,43);Draw(0); // Another instance of the same pet item.
    SetSerial(character,0x1b,43);Draw(1); // Leader switched.
    SetSerial(character,0x1b,0);SetSerial(pet,0x18,0);Draw(0); // All recalled.
    SetSerial(character,0x1b,43);SetSerial(pet,0x18,43);
    for(int type:{1,2}) {itemType=type;Draw(0);} itemType=3;
    for(int tab:{1,2,3,4}) {*reinterpret_cast<int*>(window+0x5e4)=tab;Draw(0);}
    *reinterpret_cast<int*>(window+0x5e4)=5;
    framePointer[-0x40/4]=435;framePointer[-0x34/4]=218;expectedX=433;expectedY=180;Draw(1);
    failLoad=true;Draw(0);failLoad=false;Draw(1);
    // Execute the installed native CALL and RET path, not just UpdateMark directly.
    using UpdateCall=void(__thiscall*)(void*);
    auto updateCall=reinterpret_cast<UpdateCall>(base+0x41e2c1);
    base[0x41e2c6]=0xc3; // Native epilogue pops ESI; harness has no outer frame.
    updateCall(window);Require(updates==1 && invalidations==1,"first inventory update");
    updateCall(window);Require(updates==2 && invalidations==1,"no repeated invalidation");
    SetSerial(character,0x1b,44);updateCall(window);Require(invalidations==2,"leader changed while bag open");
    SetSerial(character,0x1b,0);updateCall(window);Require(invalidations==3,"recall removes mark");
    *reinterpret_cast<void**>(context+0x20b8)=nullptr;updateCall(window);Require(invalidations==3,"no retained character pointer");
    *gameContext=nullptr;updateCall(window);Require(invalidations==3,"logout has no resource access");
    Require(mark.references==1 && destination.references==1,"COM ownership balanced");
    std::puts("PASS native draw/update bridge, signatures, 64-bit pet identity, tabs, geometry, alpha, COM ownership, resource failure, summon/recall refresh");
    CheckRealCanvas(argv[1]);
}
