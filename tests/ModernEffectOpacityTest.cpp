// Native Gr2D integration test. Own hidden render target only; never starts or captures the game.
#define NOMINMAX
#include <windows.h>
#include <cstring>
#include "../ezorsia/Memory.h"
namespace CrashReporter {void RecordEvent(const char*,const char*,...) {}}
bool Memory::SetHook(bool,void**,void*){return false;}
#include "../ezorsia/ModernCapeEffects.h"
using namespace MixedDyeResources;
#include <oleauto.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
template<class T>T method(void* p,int n){return reinterpret_cast<T>((*reinterpret_cast<void***>(p))[n/4]);}
void check(bool yes,const char* name){if(!yes){printf("FAIL: %s\n",name);exit(1);}}
void hr(HRESULT result,const char* name){check(SUCCEEDED(result),name);}
VARIANT number(int n){VARIANT v{};v.vt=VT_I4;v.lVal=n;return v;}
VARIANT unknown(void* p){VARIANT v{};v.vt=VT_UNKNOWN;v.punkVal=static_cast<IUnknown*>(p);return v;}
void release(void* p){if(p)static_cast<IUnknown*>(p)->Release();}
HRESULT __stdcall NoPresent(void*,const RECT*,const RECT*,HWND,const RGNDATA*){return S_OK;}
void replace(void** slot,void* value){DWORD old=0;check(VirtualProtect(slot,4,PAGE_READWRITE,&old)!=0,"protect");*slot=value;VirtualProtect(slot,4,old,&old);}

int main(){
    hr(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"COM");
    auto pc=LoadLibraryW(L"PCOM.dll");check(pc!=nullptr,"PCOM");
    hr(reinterpret_cast<HRESULT(__cdecl*)()>(GetProcAddress(pc,"PcInitModule"))(),"PCOM init");
    using Factory=HRESULT(__cdecl*)(const wchar_t*,const GUID*,void**,void*);
    auto create=reinterpret_cast<Factory>(GetProcAddress(pc,"PcCreateObject"));
    GUID grIID={0xe576ea33,0xd465,0x4f08,{0xaa,0xb1,0xe7,0x8d,0xf7,0x3e,0xe6,0xd9}};
    GUID canvasIID={0x7600dc6c,0x9328,0x4bff,{0x96,0x24,0x5b,0x0f,0x5c,0x01,0x17,0x9e}};
    void* gr=nullptr;hr(create(L"Gr2D_DX8",&grIID,&gr,nullptr),"create graphics");
    HWND window=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"Modern effect opacity test",WS_POPUP,-32000,-32000,800,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(window!=nullptr,"hidden window");
    VARIANT hwnd=number(int(window));hwnd.vt|=VT_BYREF;
    hr(method<HRESULT(__stdcall*)(void*,unsigned,unsigned,VARIANT,VARIANT,VARIANT)>(gr,0xc)(gr,800,600,hwnd,number(0),number(0)),"graphics init");
    hr(method<HRESULT(__stdcall*)(void*,unsigned)>(gr,0x50)(gr,0xff000000),"background");
    for(int offset:{0x54,0x58}){
        void* tone=nullptr;hr(method<HRESULT(__stdcall*)(void*,void**)>(gr,offset)(gr,&tone),"tone");
        VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;
        hr(method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(tone,0x90)(tone,255,255,missing,missing),"tone value");release(tone);
    }
    void* device=*reinterpret_cast<void**>(static_cast<char*>(gr)+0x90);
    auto deviceTable=*reinterpret_cast<void***>(device);void* originalPresent=deviceTable[15];replace(&deviceTable[15],reinterpret_cast<void*>(&NoPresent));
    auto canvas=[&](int size,unsigned color){
        void* c=nullptr;hr(create(L"Canvas",&canvasIID,&c,nullptr),"canvas");
        hr(method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(c,0x2c)(c,size,size,number(0),number(2)),"canvas alloc");
        hr(method<HRESULT(__stdcall*)(void*,int,int,int,int,unsigned)>(c,0x8c)(c,0,0,size,size,color),"canvas fill");return c;
    };
    auto layer=[&](void* source,int x,int y,int size,void* parent,int z=1){
        void* value=nullptr;VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;
        hr(method<HRESULT(__stdcall*)(void*,int,int,unsigned,unsigned,int,VARIANT,VARIANT,void**)>(gr,0x64)(gr,x,y,size,size,z,unknown(source),missing,&value),"layer");
        if(parent)for(int offset:{0x64,0xfc})hr(method<HRESULT(__stdcall*)(void*,VARIANT)>(value,offset)(value,unknown(parent)),"layer parent");
        hr(method<HRESULT(__stdcall*)(void*,unsigned)>(value,0xe0)(value,0xffffffff),"layer alpha");
        hr(method<HRESULT(__stdcall*)(void*,int)>(value,0x11c)(value,1),"layer visibility");return value;
    };


    auto cyan=canvas(20,0xff00ffff);
    auto root=layer(cyan,-100,0,20,nullptr);
    auto body=New(create,L"Shape2D#Vector2D",VectorIID());
    VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;
    hr(method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(body.p,0x90)(body.p,40,0,missing,missing),"body origin");
    BYTE avatar[0x1100]{};*reinterpret_cast<void**>(avatar+0x10b8)=body.p;
    auto color=[&](void* p){unsigned value=0;hr(method<HRESULT(__stdcall*)(void*,unsigned*)>(p,0xdc)(p,&value),"get color");return value;};
    auto setColor=[&](void* p,unsigned value){hr(method<HRESULT(__stdcall*)(void*,unsigned)>(p,0xe0)(p,value),"set color");};
    auto makeEffect=[&](bool fading){
        auto effect=New(create,L"Property",PropertyIID()),frames=New(create,L"Property",PropertyIID());
        Put(effect.p,L"default",unknown(frames.p));
        for(int i=0;i<2;i++){
            auto frame=canvas(20,0xffff0000);auto meta=CanvasProperty(frame);
            Put(meta.p,L"delay",number(fading?200:80));
            Put(meta.p,L"a0",number(fading&&i?0:255));Put(meta.p,L"a1",number(fading&&!i?0:255));
            Put(frames.p,std::to_wstring(i),unknown(frame));release(frame);
        }
        return effect;
    };
    auto sample=[&](){
        for(int i=0;i<3;i++){
            int next=0;hr(method<HRESULT(__stdcall*)(void*,int*)>(gr,0x14)(gr,&next),"next time");
            hr(method<HRESULT(__stdcall*)(void*,int)>(gr,0x18)(gr,next),"update time");
            hr(method<HRESULT(__stdcall*)(void*)>(gr,0x1c)(gr),"render");
        }
        // Copy only our back render target. Never calls GetFrontBuffer or captures desktop.
        void *target=nullptr,*copy=nullptr;hr(method<HRESULT(__stdcall*)(void*,void**)>(device,0x80)(device,&target),"render target");
        struct Desc{unsigned format,type,usage,pool,size,multisample,w,h;}desc{};
        hr(method<HRESULT(__stdcall*)(void*,Desc*)>(target,0x20)(target,&desc),"surface desc");check(desc.format==21||desc.format==22,"32-bit surface");
        hr(method<HRESULT(__stdcall*)(void*,unsigned,unsigned,unsigned,void**)>(device,0x6c)(device,desc.w,desc.h,desc.format,&copy),"readback surface");
        RECT area{0,0,LONG(desc.w),LONG(desc.h)};POINT dest{};
        hr(method<HRESULT(__stdcall*)(void*,void*,const RECT*,unsigned,void*,const POINT*)>(device,0x70)(device,target,&area,1,copy,&dest),"copy target");
        struct Locked{int pitch;void* bits;}locked{};
        hr(method<HRESULT(__stdcall*)(void*,Locked*,const RECT*,unsigned)>(copy,0x24)(copy,&locked,nullptr,0x10),"lock readback");
        std::vector<unsigned> pixels(desc.w*desc.h);
        for(unsigned y=0;y<desc.h;y++)std::memcpy(pixels.data()+y*desc.w,static_cast<char*>(locked.bits)+y*locked.pitch,desc.w*4);
        hr(method<HRESULT(__stdcall*)(void*)>(copy,0x28)(copy),"unlock readback");release(copy);release(target);return pixels;
    };
    int cameraX=0,cameraY=0;
    auto pixel=[&](const std::vector<unsigned>& image,int x,int y){return image[(y-cameraY+300)*800+x-cameraX+400]&0xffffff;};


    // A real renderer is necessary: a mock put_color cannot reproduce Animate
    // overwriting the very same alpha vector at a canvas boundary.
    auto opaque=makeEffect(false);
    setColor(root,0x80ffffff);
    auto old=ModernCapeEffects::BuildLayer(opaque.p,L"default",gr,body.p,root,0);
    unsigned low=255,high=0;
    for(int i=0;i<40;i++){
        setColor(old.p,color(root));sample();auto a=color(old.p)>>24;
        low=(std::min)(low,a);high=(std::max)(high,a);Sleep(16);
    }
    printf("old path reproduced: opacity %u..%u while owner=128\n",low,high);
    check(low==128&&high==255,"old per-tick copy flashes on actual native frame changes");
    release(old.detach());
    ModernCapeEffects::State state;state.effect.p=opaque.detach();
    for(unsigned opacity:{255u,128u,0u,128u,255u}){
        setColor(root,(opacity<<24)|0xffffff);
        ModernCapeEffects::Sync(state,gr,avatar,root,L"stand1",0);
        auto retained=state.layer.p;
        for(int i=0;i<40;i++){
            ModernCapeEffects::Sync(state,gr,avatar,root,L"stand1",i&1);
            check(state.layer.p==retained,"steady color and turning preserve animation object");
            auto image=sample();
            check((color(state.layer.p)>>24)==opacity,"stable native animation opacity across canvas changes");
            // Turning mirrors the square around its origin; sample its inside.
            auto redPixel=(pixel(image,(i&1)?35:45,5)>>16)&255;
            check(abs(int(redPixel)-int(opacity))<=1,"rendered effect pixels keep owner opacity");
            check(abs(int((pixel(image,-95,5)>>8)&255)-int(opacity))<=1,"owner layer remains unchanged");
            Sleep(16);
        }
        printf("PASS actual rendered frames: owner opacity=%u\n",opacity);
    }
    hr(method<HRESULT(__stdcall*)(void*,int)>(root,0x11c)(root,0),"owner invisible");
    ModernCapeEffects::Sync(state,gr,avatar,root,L"stand1",0);check(pixel(sample(),45,5)==0,"full visibility suppression");
    hr(method<HRESULT(__stdcall*)(void*,int)>(root,0x11c)(root,1),"owner visible");
    ModernCapeEffects::Sync(state,gr,avatar,root,L"stand1",0);check(pixel(sample(),45,5)==0xff0000,"visibility restores");
    ModernCapeEffects::Reset(state);
    auto fading=makeEffect(true);state.effect.p=fading.detach();
    for(unsigned opacity:{128u,255u}){
        setColor(root,(opacity<<24)|0xffffff);low=255;high=0;
        for(int i=0;i<80;i++){
            ModernCapeEffects::Sync(state,gr,avatar,root,L"stand1",0);
            auto image=sample();auto a=color(state.layer.p)>>24;
            low=(std::min)(low,a);high=(std::max)(high,a);
            check(a<=opacity,"source fade never exceeds owner opacity");
            check(abs(int((pixel(image,45,5)>>16)&255)-int(a))<=1,"fade is rendered, not only bookkeeping");Sleep(16);
        }
        printf("PASS source fade: owner=%u animated=%u..%u\n",opacity,low,high);
        check(low<opacity/4&&high>opacity*3/4,"original bidirectional fade continues during repeated updates");
    }
    auto first=Resolve(state.effect.p,L"default/0");auto frame=Query(first.v,CanvasIID());auto meta=CanvasProperty(frame.p);
    check(ModernCapeEffects::Number(meta.p,L"a0",-1)==255&&ModernCapeEffects::Number(meta.p,L"a1",-1)==0,"source frame metadata unchanged");
    release(meta.detach());release(frame.detach()); // Release all PCOM objects before PcTermModule.
    ResourceVariant::Clear(&first.v);ModernCapeEffects::Reset(state);release(body.detach());
    release(root);release(cyan);
    replace(&deviceTable[15],originalPresent);
    hr(method<HRESULT(__stdcall*)(void*)>(gr,0x10)(gr),"graphics uninitialize");release(gr);DestroyWindow(window);
    reinterpret_cast<void(__cdecl*)()>(GetProcAddress(pc,"PcTermModule"))();CoUninitialize();
    puts("PASS native GM hide/unhide, turning, frame boundaries, source fades and visibility; no game process");return 0;
}
