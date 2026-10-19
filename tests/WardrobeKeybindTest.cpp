#include <windows.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include "WardrobeKeybindUnderTest.h"

bool Client::enableMovementKeyRebind=false;
static void Require(bool ok,const char* what){if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
// Only replace downstream UI/network work; the callback itself comes from WardrobeWnd.cpp.
namespace WardrobePreview {struct ExternalScope {};}
namespace WardrobeInput {
bool Locked(){return false;}
bool ConsumeAfterClose(unsigned,unsigned){return false;}
}
namespace CrashReporter {void RecordEvent(const char*,const char*,...) {}}
struct {bool shown=false,field=true,opening=false;int edit=-1;} state;
static int opened=0,forwarded=0,windowKeys=0;
static bool FinishDeferredClose(){return false;}
static void Key(void*,void*,unsigned,unsigned){++windowKeys;}
static void OpenWindow(){++opened;}
static void Fail(const char*){Require(false,"entry callback did not throw");}
static void __fastcall ForwardKey(void*,void*,unsigned,unsigned){++forwarded;}
static auto rootKey=&ForwardKey;
#include "WardrobeRootKeyUnderTest.h"
static void* initializerDetour=nullptr;
bool Memory::SetHook(bool,void**,void* detour){initializerDetour=detour;return true;}
void Memory::WriteByte(DWORD address,unsigned char value){*reinterpret_cast<unsigned char*>(address)=value;}
void Memory::WriteInt(DWORD address,unsigned value){std::memcpy(reinterpret_cast<void*>(address),&value,4);}
void Memory::CodeCave(void* function,DWORD address,int){WriteByte(address,0xe9);WriteInt(address+1,DWORD(function)-address-5);}
static void MapClient(const wchar_t* path){
    FILE* f=nullptr;Require(!_wfopen_s(&f,path,L"rb"),"read native EXE");
    unsigned char header[4096];Require(fread(header,1,sizeof(header),f)==sizeof(header),"PE header");
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(header+reinterpret_cast<IMAGE_DOS_HEADER*>(header)->e_lfanew);
    Require(nt->FileHeader.TimeDateStamp==0x4b7c15c9 && nt->OptionalHeader.SizeOfImage==0xa94000,"supported v83 image");
    auto base=static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(0x30400000),nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Require(base==reinterpret_cast<void*>(0x30400000),"isolated mapping");
    auto sections=IMAGE_FIRST_SECTION(nt);
    for(int i=0;i<nt->FileHeader.NumberOfSections;i++){
        fseek(f,sections[i].PointerToRawData,SEEK_SET);
        Require(fread(base+sections[i].VirtualAddress,1,sections[i].SizeOfRawData,f)==sections[i].SizeOfRawData,"PE section");
    }
    fclose(f); // Never invoke the client entry point or initialize the game.
}
int wmain(int argc,wchar_t** argv){
    Require(argc==2,"EXE path");MapClient(argv[1]);
    using namespace WardrobeKeybind;
    Require(Install(),"exact native signatures and installation");
    Require(!Install(),"duplicate installation rejected");
    Require(*reinterpret_cast<int*>(0x00833FB0)==0x00BE2C00,"tray includes template 40");
    auto f8=reinterpret_cast<unsigned char*>(0x00BD8BCC+66*5);
    Require(f8[0]==4 && *reinterpret_cast<int*>(f8+1)==40,"restore defaults includes F8");
    using Icon=int(__fastcall*)(void*,void*,int);
    auto icon=reinterpret_cast<Icon>(0x00836C82);
    for(int i=0;i<40;i++)Require(icon(nullptr,nullptr,i)==(i<28?i:i<33?i+22:i+67),"existing template icons preserved");
    Require(icon(nullptr,nullptr,40)==40,"wardrobe icon identity");
    std::vector<unsigned char> window(0x9dc+54*12+16,0xcd);
    reinterpret_cast<Initialize>(initializerDetour)(window.data(),nullptr);
    auto entry=window.data()+0x9dc+40*12;
    Require(entry[0]==4 && *reinterpret_cast<int*>(entry+1)==40 && *reinterpret_cast<int*>(entry+8)==0,"native initialization plus wardrobe template");
    Require(entry[12]==0xcd,"initialization stays in allocated slot");
    using ActionIndex=int(__thiscall*)(void*,unsigned char,int);
    Require(reinterpret_cast<ActionIndex>(0x00836C5F)(window.data(),4,40)==40,"native drag/drop action-to-template mapping");
    unsigned char map[4+90*5]{};*reinterpret_cast<void**>(0x00BED5A0)=map;
    auto set=[&](int scan,int type,int action){map[4+scan*5]=static_cast<unsigned char>(type);std::memcpy(map+5+scan*5,&action,4);};
    set(66,4,40);Require(Matches(VK_F8,66u<<16),"F8 reads active keymap");
    unsigned char manager[0xb0]{},chat[16]{};
    *reinterpret_cast<void**>(0x00BEC20C)=manager;
    *reinterpret_cast<void**>(manager+0x88)=manager+4;
    *reinterpret_cast<void**>(0x00BE7910)=nullptr;
    // Native CWndMan::OnKey is an IUIMsgHandler callback: ECX is manager+4.
    RootKey(manager+4,nullptr,VK_F8,66u<<16);
    Require(opened==1 && forwarded==0,"F8 opens from native manager+4 callback");
    RootKey(manager+4,nullptr,VK_F8,(66u<<16)|0x40000000u);
    RootKey(manager+4,nullptr,VK_F8,(66u<<16)|0x80000000u);
    Require(opened==1 && forwarded==0,"repeat and key-up do not reopen");
    *reinterpret_cast<void**>(manager+0x88)=chat;
    RootKey(manager+4,nullptr,VK_F8,66u<<16);
    Require(opened==1 && forwarded==1,"focused chat forwards instead of opening");
    *reinterpret_cast<void**>(manager+0x88)=manager+4;
    *reinterpret_cast<void**>(0x00BE7910)=chat;
    RootKey(manager+4,nullptr,VK_F8,66u<<16);
    Require(opened==1 && forwarded==2,"modal dialog blocks opening");
    *reinterpret_cast<void**>(0x00BE7910)=nullptr;
    *reinterpret_cast<void**>(0x00BEC20C)=nullptr;
    RootKey(manager+4,nullptr,VK_F8,66u<<16);
    Require(opened==1 && forwarded==3,"missing manager safely forwards");
    *reinterpret_cast<void**>(0x00BEC20C)=manager;
    set(66,3,5160008);set(67,4,40);
    Require(!Matches(VK_F8,66u<<16) && Matches(VK_F9,67u<<16),"rebinding preserves old F8 action");
    RootKey(manager+4,nullptr,VK_F8,66u<<16);
    Require(opened==1 && forwarded==4,"occupied F8 keeps its native action");
    RootKey(manager+4,nullptr,VK_F9,67u<<16);
    Require(opened==2,"rebound wardrobe key opens through native callback");
    set(67,0,0);Require(!Matches(VK_F9,67u<<16),"unbinding is respected");
    for(unsigned scan:{0x52u,0x47u,0x49u,0x53u,0x4fu,0x51u,0x1du,0x38u}){
        set(scan,4,40);Require(Matches(0,(scan<<16)|0x01000000u),"extended navigation/modifier keys use the native scan slot");set(scan,0,0);
    }
    set(0x2a,4,40);Require(Matches(VK_RSHIFT,0x36u<<16)&&Matches(VK_LSHIFT,0x2au<<16),"both Shift keys use one binding");
    Require(!Matches(VK_F8,0xffu<<16),"out-of-range scan rejected");
    std::puts("PASS native key signatures, initialization, tray bounds, icons, defaults, rebinding and actual RootKey focus routing");
}
