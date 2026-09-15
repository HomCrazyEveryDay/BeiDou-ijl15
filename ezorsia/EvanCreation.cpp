#include "stdafx.h"
#include "EvanCreation.h"
#include "MapleClientCollectionTypes/ZXString.h"
#include <array>
#include <cstring>
#include <cwchar>
#include "EvanCreationOptions.h"

// GMS 83 CRaceSelect uses a 700x460 window. Keep its camera/stage geometry and
// render GMS 84's four original 286x216 cards inside it. The existing name and
// avatar editors remain native; Evan uses their Explorer layout with its own
// resource paths, choices, and wire creation type 3.
namespace {
struct Variant { DWORD data[4]{}; };
struct Card { DWORD normal=0, hover=0, pressed=0, focused=0; };
std::array<Card,4> cards;
constexpr int races[4]={0,1,2,3};
constexpr wchar_t const* paths[4]={L"BtKnight",L"BtNormal",L"BtAran",L"BtEvan"};
unsigned char* window=nullptr;
unsigned char* login=nullptr;
int selected=1,hovered=-1,pressed=-1;
bool evan=false,ready=false;
DWORD wireReturn=0x005F7F0A;
using StageFn=void(__fastcall*)(void*,void*,int);
using ResetFn=void(__fastcall*)(void*,void*);
using StringFn=ZXString<wchar_t>*(__fastcall*)(void*,void*,ZXString<wchar_t>*,unsigned int,char);
auto changeStage=reinterpret_cast<StageFn>(0x005F53C0);
auto resetChoices=reinterpret_cast<ResetFn>(0x005FCF8A);
auto getStringW=reinterpret_cast<StringFn>(0x0079EA63);

void Release(DWORD& p) {
    if(p) { auto v=*reinterpret_cast<DWORD**>(p);reinterpret_cast<ULONG(__stdcall*)(DWORD)>(v[2])(p);p=0; }
}
DWORD LoadCanvas(const wchar_t* path) {
    unsigned char holder[0x100]{};DWORD name=0;
    reinterpret_cast<void*(__fastcall*)(void*,void*)>(0x009DE383)(holder,nullptr);
    reinterpret_cast<DWORD*(__fastcall*)(DWORD*,void*,const wchar_t*)>(0x00403382)(&name,nullptr,path);
    reinterpret_cast<void(__fastcall*)(void*,void*,DWORD,int,int)>(0x009E0AB2)(holder,nullptr,name,0,0);
    DWORD canvas=*reinterpret_cast<DWORD*>(holder+0x68);
    *reinterpret_cast<DWORD*>(holder+0x68)=0;
    reinterpret_cast<void(__fastcall*)(void*,void*)>(0x009DE438)(holder+8,nullptr);
    return canvas;
}
void DrawCanvas(DWORD target,DWORD source,int x,int y) {
    if(!source)return;
    auto v=*reinterpret_cast<DWORD**>(target);Variant alpha;alpha.data[0]=3;alpha.data[2]=255;
    reinterpret_cast<HRESULT(__stdcall*)(DWORD,int,int,DWORD,Variant)>(v[0x80/4])(target,x,y,source,alpha);
}
void Invalidate() {
    if(window)reinterpret_cast<void(__fastcall*)(void*,void*,const RECT*)>(0x009E04C9)(window,nullptr,nullptr);
}
bool CanSelect() {
    return ready && login && *reinterpret_cast<int*>(login+0x168)==3
        && *reinterpret_cast<int*>(login+0x16c)==0 && *reinterpret_cast<int*>(login+0x170)==0;
}
int Hit(int x,int y) {
    for(int i=0;i<4;++i){int left=48+(i%2)*304,top=8+(i/2)*228;
        if(x>=left&&x<left+286&&y>=top&&y<top+216)return i;}
    return -1;
}
void Choose(int index) {
    if(index<0||index>3||!CanSelect())return;
    selected=index;evan=races[index]==3;
    // Logical wire type is separate from the reused native editor geometry.
    *reinterpret_cast<int*>(login+0x214)=evan?1:races[index];
    *reinterpret_cast<int*>(window+0x70)=evan?1:races[index];
    changeStage(login,nullptr,4);
}
void __fastcall OnCreate(void* self,void*,void*) {
    window=static_cast<unsigned char*>(self);login=*reinterpret_cast<unsigned char**>(window+0x6c);
    selected=1;hovered=pressed=-1;evan=false;ready=true;
    for(int i=0;i<4;++i){
        DWORD* targets[]={&cards[i].normal,&cards[i].hover,&cards[i].pressed,&cards[i].focused};
        const wchar_t* states[]={L"normal",L"mouseOver",L"pressed",L"keyFocused"};
        for(int j=0;j<4;++j){
            if(!*targets[j]){wchar_t path[180];swprintf_s(path,L"UI/Login.img/RaceSelectV84/%ls/%ls/0",paths[i],states[j]);*targets[j]=LoadCanvas(path);}
            ready=ready&&*targets[j]!=0;
        }
    }
}
void __fastcall Draw(void* self,void*,const RECT* rect) {
    reinterpret_cast<void(__fastcall*)(void*,void*,const RECT*)>(0x009E0502)(self,nullptr,rect);
    DWORD target=0;
    reinterpret_cast<DWORD*(__fastcall*)(void*,void*,DWORD*)>(0x00425C4C)(self,nullptr,&target);
    if(!target)return;
    for(int i=0;i<4;++i){auto& card=cards[i];DWORD canvas=card.normal;
        if(i==pressed&&i==hovered)canvas=card.pressed;
        else if(i==hovered)canvas=card.hover;
        else if(i==selected)canvas=card.focused;
        DrawCanvas(target,canvas,48+(i%2)*304,8+(i/2)*228);
    }
    Release(target);
}
void __fastcall Update(void*,void*) {} // v83's three-card animation updater must not run.
void __fastcall MouseButton(void*,void*,unsigned int message,unsigned int,int x,int y) {
    int index=Hit(x,y);
    if(message==WM_LBUTTONDOWN){pressed=index;hovered=index;Invalidate();}
    else if(message==WM_LBUTTONUP){int was=pressed;pressed=-1;Invalidate();if(index>=0&&index==was)Choose(index);}
}
int __fastcall MouseMove(void*,void*,int x,int y) {int n=Hit(x,y);if(n!=hovered){hovered=n;Invalidate();}return 0;}
void __fastcall MouseEnter(void*,void*,int enter) {if(!enter){hovered=pressed=-1;Invalidate();}}
void __fastcall Key(void*,void*,unsigned int key,unsigned int flags) {
    if(flags&0x80000000U || !CanSelect())return;
    if(key==VK_ESCAPE){evan=false;changeStage(login,nullptr,2);return;}
    if(key==VK_RETURN){Choose(selected);return;}
    if(key==VK_LEFT||key==VK_RIGHT)selected^=1;
    else if(key==VK_UP||key==VK_DOWN)selected^=2;
    else if(key==VK_TAB)selected=(selected+1)%4;
    else return;
    Invalidate();
}
void __fastcall Button(void*,void*,unsigned int) {}
void __fastcall DoubleClick(void*,void*,unsigned int) {}
void __fastcall Stage(void* self,void*,int stage) {
    int old=*reinterpret_cast<int*>(static_cast<unsigned char*>(self)+0x168);
    int next=stage<0?(old+1)%6:stage;
    if(next!=4&&next!=5){evan=false;selected=1;hovered=pressed=-1;}
    changeStage(self,nullptr,stage);
    if(next==3)Invalidate();
}
void __fastcall ResetChoices(void* self,void*) {
    resetChoices(self,nullptr);
    if(!evan||self!=login)return;
    auto clear=reinterpret_cast<void(__fastcall*)(void*,void*)>(0x005FE122);
    auto append=reinterpret_cast<void*(__fastcall*)(void*,void*,int)>(0x005FE15C);
    for(int gender=0;gender<2;++gender)for(int part=0;part<9;++part){
        void* list=login+(gender?0x268:0x244)+part*4;clear(list,nullptr);
        const auto& values=kEvanOptions[gender][part];
        for(int j=0;j<values.count;++j){
            auto item=static_cast<unsigned char*>(append(list,nullptr,-1));
            *reinterpret_cast<int*>(item)=values.values[j];
            char text[48];
            if(part==8)strcpy_s(text,gender?"\xC5\xAE":"\xC4\xD0");
            else sprintf_s(text,"%s %d",kEvanOptionLabels[part],j+1);
            *reinterpret_cast<ZXString<char>*>(item+4)=text;
        }
    }
}
ZXString<wchar_t>* __fastcall StringW(void* self,void*,ZXString<wchar_t>* result,unsigned int id,char formal) {
    auto value=getStringW(self,nullptr,result,id,formal);
    if(evan&&id>=1334&&id<=1344&&value){
        const wchar_t* suffix[]={L"charName",L"charSet",L"avatarSel",L"BtLeft",L"BtRight",L"scroll/0",L"BtCheck",L"BtYes",L"BtNo",L"dice",L"statTb"};
        wchar_t path[128];swprintf_s(path,L"UI/Login.img/NewCharEvan/%ls",suffix[id-1334]);*value=path;
    }
    return value;
}
__declspec(naked) void WireRace() {
    __asm {
        pushfd
        cmp byte ptr [evan],0
        je original
        cmp esi,dword ptr [login]
        jne original
        popfd
        push 3
        jmp dword ptr [wireReturn]
    original:
        popfd
        push dword ptr [esi+214h]
        jmp dword ptr [wireReturn]
    }
}
}
bool EvanCreation::Install() {
    struct Patch {DWORD address,expected;void* replacement;};
    const Patch patches[]={
        {0x00AF7360,0x006173B3,reinterpret_cast<void*>(&Update)},
        {0x00AF736C,0x006157C9,reinterpret_cast<void*>(&OnCreate)},
        {0x00AF7380,0x0061747F,reinterpret_cast<void*>(&Button)},
        {0x00AF738C,0x009E0502,reinterpret_cast<void*>(&Draw)},
        {0x00AF7394,0x006174CF,reinterpret_cast<void*>(&DoubleClick)},
        {0x00AF7314,0x00617514,reinterpret_cast<void*>(&Key)},
        {0x00AF731C,0x00424443,reinterpret_cast<void*>(&MouseButton)},
        {0x00AF7320,0x00424446,reinterpret_cast<void*>(&MouseMove)},
        {0x00AF7328,0x009E01C8,reinterpret_cast<void*>(&MouseEnter)}
    };
    for(auto& p:patches)if(*reinterpret_cast<DWORD*>(p.address)!=p.expected)return false;
    const unsigned char wire[]={0xff,0xb6,0x14,0x02,0x00,0x00};
    if(memcmp(reinterpret_cast<void*>(0x005F7F04),wire,sizeof(wire)))return false;
    const unsigned char stageBytes[]={0xb8,0x6b,0x77,0xa9,0,0xe8,0xce,0xb7,0x46,0};
    const unsigned char resetBytes[]={0xb8,0xe8,0x81,0xa9,0,0xe8,0x04,0x3c,0x46,0};
    const unsigned char stringBytes[]={0xb8,0xc6,0x70,0xab,0,0xe8,0x2b,0x21,0x2c,0};
    if(memcmp(reinterpret_cast<void*>(0x005F53C0),stageBytes,sizeof(stageBytes))
        ||memcmp(reinterpret_cast<void*>(0x005FCF8A),resetBytes,sizeof(resetBytes))
        ||memcmp(reinterpret_cast<void*>(0x0079EA63),stringBytes,sizeof(stringBytes)))return false;
    if(!Memory::SetHook(true,reinterpret_cast<void**>(&changeStage),reinterpret_cast<void*>(&Stage)))return false;
    if(!Memory::SetHook(true,reinterpret_cast<void**>(&resetChoices),reinterpret_cast<void*>(&ResetChoices))){
        Memory::SetHook(false,reinterpret_cast<void**>(&changeStage),reinterpret_cast<void*>(&Stage));return false;}
    if(!Memory::SetHook(true,reinterpret_cast<void**>(&getStringW),reinterpret_cast<void*>(&StringW))){
        Memory::SetHook(false,reinterpret_cast<void**>(&resetChoices),reinterpret_cast<void*>(&ResetChoices));
        Memory::SetHook(false,reinterpret_cast<void**>(&changeStage),reinterpret_cast<void*>(&Stage));return false;}
    Memory::CodeCave(&WireRace,0x005F7F04,6);
    for(auto& p:patches)Memory::WriteInt(p.address,reinterpret_cast<DWORD>(p.replacement));
    return true;
}
