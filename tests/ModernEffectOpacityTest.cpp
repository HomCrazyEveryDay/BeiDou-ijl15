// Native Gr2D integration test. Own hidden render target only; never starts or captures the game.
#define NOMINMAX
#include <windows.h>
#include <cstring>
#include "../ezorsia/Memory.h"
namespace CrashReporter {void RecordEvent(const char*,const char*,...) {}}
bool Memory::SetHook(bool,void**,void*){return false;}
#include "../ezorsia/ModernCapeEffects.h"
#include "../ezorsia/ChairCompatibility.cpp"
namespace ClientLog {void Append(Component,const char*,...) {}}
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
    setvbuf(stdout,nullptr,_IONBF,0);
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

    // Native overlay depth and vector coordinates, without a game window.
    {
        auto parent=layer(cyan,120,0,20,nullptr,100);
        auto red=canvas(20,0xffff0000);
        auto behind=layer(red,0,0,20,parent,-3);
        check(pixel(sample(),125,5)==0x00ffff,"negative overlay depth is rendered behind owner");
        check(Int(behind,0x20)==120,"native layer coordinates include the parent origin");
        hr(method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(behind,0x90)(behind,7,8,missing,missing),"pet initial offset");
        PetSeatState seat;
        SavePetLayer(seat,behind);
        hr(method<HRESULT(__stdcall*)(void*,VARIANT)>(behind,0x64)(behind,unknown(body.p)),"chair origin");
        hr(method<HRESULT(__stdcall*)(void*,VARIANT)>(behind,0xfc)(behind,unknown(root)),"chair overlay");
        hr(method<HRESULT(__stdcall*)(void*,int)>(behind,0xb4)(behind,4),"chair depth");
        hr(method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(parent,0x90)(parent,200,50,missing,missing),"moving pet parent");
        RestorePetLayer(seat);
        check(Int(behind,0x20)==207 && Int(behind,0x28)==58 && Int(behind,0xb0)==-3,"standing restores local pet position and depth after parent movement");
        printf("PASS native pet origin, overlay and depth restoration with moving parent\n");
        release(behind);release(red);release(parent);
    }

    // Exercise installed artwork through the production resource reader and
    // layer builder. An opaque owner must hide every overlapping effect pixel;
    // moving that same effect in front is the positive control for visibility.
    {
        const GUID rmId={0x57dfe40b,0x3e20,0x4dbc,{0x97,0xe8,0x80,0x5a,0x50,0xf3,0x81,0xbf}};
        const GUID nsId={0x2aeeeb36,0xa4e1,0x4e2b,{0x8f,0x6f,0x2e,0x7b,0xde,0xc5,0xc5,0x3d}};
        const GUID fsId={0x352d8655,0x51e4,0x4668,{0x8c,0xe4,0x08,0x66,0xe2,0xb6,0xa5,0xb5}};
        auto rm=New(create,L"ResMan",rmId),ns=New(create,L"NameSpace",nsId),fs=New(create,L"NameSpace#FileSystem",fsId);
        Check(Method<HRESULT(__stdcall*)(void*,int,int,int)>(rm.p,0x14)(rm.p,0x11,-1,-1));
        Check(reinterpret_cast<HRESULT(__cdecl*)(void**,int)>(GetProcAddress(pc,"PcRootNameSpace"))(&ns.p,1));
        BSTR path=SysAllocString(L"Data");Check(Method<HRESULT(__stdcall*)(void*,BSTR)>(fs.p,0x34)(fs.p,path));SysFreeString(path);
        path=SysAllocString(L"/");Check(Method<HRESULT(__stdcall*)(void*,BSTR,void*,int)>(ns.p,0x18)(ns.p,path,fs.p,0));SysFreeString(path);
        auto shield=canvas(500,0xff00ffff);
        auto owner=layer(shield,-250,-250,500,nullptr,100);
        auto center=New(create,L"Shape2D#Vector2D",VectorIID());
        BYTE look[0x1100]{};
        *reinterpret_cast<void**>(look+0x10b4)=center.p;
        *reinterpret_cast<void**>(look+0x10b8)=center.p;
        *reinterpret_cast<void**>(look+0x10c8)=owner;
        auto changed=[&](const std::vector<unsigned>& pixels){
            int count=0;
            for(int y=-240;y<240;++y)for(int x=-240;x<240;++x)if(pixel(pixels,x,y)!=0x00ffff)++count;
            return count;
        };
        for(int id:{1103654,1103946,1103649,1073379,1103552,1103949}) {
            int cases=0,visibleControls=0;
            auto effect=ModernCapeEffects::EffectFor(id,rm.p);
            check(effect.p!=nullptr,"installed feedback item effect exists");
            for(const auto& action:Names(effect.p)) {
                auto branch=Resolve(effect.p,action);
                if(!Query(branch.v,PropertyIID()).p)continue;
                auto first=Resolve(effect.p,action+L"/0");
                if(!Query(first.v,CanvasIID()).p)continue;
                for(int flip:{0,1}) {
                    ModernCapeEffects::State actual;
                    static_cast<IUnknown*>(effect.p)->AddRef();actual.effect.p=effect.p;
                    ModernCapeEffects::Sync(actual,gr,look,owner,action,flip);
                    check(actual.layer.p!=nullptr,"production layer exists for source action");
                    check(changed(sample())==0,"installed effect is occluded by its owner in both directions");
                    hr(method<HRESULT(__stdcall*)(void*,int)>(actual.layer.p,0xb4)(actual.layer.p,3),"positive control depth");
                    if(changed(sample())>0)++visibleControls;
                    ModernCapeEffects::Reset(actual);++cases;
                }
            }
            check(cases>0 && visibleControls==cases,"all tested source actions visibly render in the front control");
            printf("PASS installed item=%d: %d action/facing renders behind owner; %d front controls visible\n",id,cases,visibleControls);
            const int slot=id/10000==107?7:9;
            std::memcpy(look+0x1d+slot*4,&id,4);
            void* retained=nullptr;
            for(int facing:{0,1,0}) {
                const int imageFlip=1-facing; // Native standing: right=0/flip=1, left=1/flip=0.
                *reinterpret_cast<int*>(look+0x4e8)=4|facing;
                hr(method<HRESULT(__stdcall*)(void*,int)>(owner,0xd8)(owner,imageFlip),"native owner facing");
                hr(method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(owner,0x90)(owner,imageFlip?250:-250,-250,missing,missing),"keep flipped owner centered");
                ModernCapeEffects::TickEffects(look,rm.p,gr,L"stand1");
                auto actual=ModernCapeEffects::States().at(look)[slot].layer.p;
                check(actual && Int(actual,0xd4)==imageFlip,"production dispatch uses native image flip instead of movement facing");
                check(!retained || retained==actual,"turning preserves the effect animation clock");retained=actual;
                hr(method<HRESULT(__stdcall*)(void*,int)>(actual,0xb4)(actual,3),"position-only front control");
                auto pixels=sample();long long xSum=0;int count=0;
                for(int y=-240;y<240;++y)for(int x=-240;x<240;++x)if(pixel(pixels,x,y)!=0x00ffff){xSum+=x;++count;}
                check(count>0 && (facing?xSum>0:xSum<0),"actual source artwork stays on the side behind the character's facing");
            }
            printf("PASS installed item=%d: right-facing effect on left, left-facing effect on right, no restart on turning\n",id);
            ModernCapeEffects::States().erase(look);int empty=0;std::memcpy(look+0x1d+slot*4,&empty,4);
            hr(method<HRESULT(__stdcall*)(void*,int)>(owner,0xd8)(owner,0),"restore owner flip");
            hr(method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(owner,0x90)(owner,-250,-250,missing,missing),"restore owner position");
        }
        release(owner);release(shield);
    }


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
