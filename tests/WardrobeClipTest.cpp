// Native graphics integration test. Own hidden window/render target only; no game process.
#include <windows.h>
#include <oleauto.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../ezorsia/Memory.h"
#include "../ezorsia/detours.h"
#include "../ezorsia/WardrobeClip.h"
template<class T>T method(void* p,int n){return reinterpret_cast<T>((*reinterpret_cast<void***>(p))[n/4]);}
void check(bool yes,const char* name){if(!yes){printf("FAIL: %s\n",name);exit(1);}}
void hr(HRESULT result,const char* name){check(SUCCEEDED(result),name);}
VARIANT number(int n){VARIANT v{};v.vt=VT_I4;v.lVal=n;return v;}
VARIANT unknown(void* p){VARIANT v{};v.vt=VT_UNKNOWN;v.punkVal=static_cast<IUnknown*>(p);return v;}
void release(void* p){if(p)static_cast<IUnknown*>(p)->Release();}
bool Memory::SetHook(bool attach,void** target,void* hook){
    if(DetourTransactionBegin()!=NO_ERROR)return false;
    if(DetourUpdateThread(GetCurrentThread())!=NO_ERROR || (attach?DetourAttach(target,hook):DetourDetach(target,hook))!=NO_ERROR){DetourTransactionAbort();return false;}
    return DetourTransactionCommit()==NO_ERROR;
}
HRESULT __stdcall NoPresent(void*,const RECT*,const RECT*,HWND,const RGNDATA*){return S_OK;}
using SetPlane=HRESULT(__stdcall*)(void*,unsigned,const float*);
SetPlane setPlane=nullptr;unsigned planeWrites=0;
HRESULT __stdcall CountPlane(void* device,unsigned index,const float* plane){++planeWrites;return setPlane(device,index,plane);}
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
    HWND window=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"Wardrobe component test",WS_POPUP,-32000,-32000,800,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
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
    auto cyan=canvas(100,0xff00ffff),red=canvas(100,0xffff0000),blue=canvas(20,0xff0000ff),green=canvas(20,0xff00ff00);
    auto root=layer(cyan,100,100,100,nullptr),child=layer(red,50,50,100,root),grandchild=layer(blue,70,70,20,child),sibling=layer(green,300,50,20,nullptr);
    float originalPlanes[4][4]{};for(unsigned i=0;i<4;i++){
        originalPlanes[i][0]=1;originalPlanes[i][3]=float(17+i);
        hr(method<HRESULT(__stdcall*)(void*,unsigned,const float*)>(device,0xc0)(device,i,originalPlanes[i]),"seed planes");
    }
    hr(method<HRESULT(__stdcall*)(void*,int,unsigned)>(device,0xc8)(device,152,0),"disable planes");
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
    auto normal=sample();check(pixel(normal,225,225)==0xff,"unclipped grandchild");
    check(WardrobeClip::Install(gr),"install actual production clipping module");WardrobeClip::SetRoot(root,0,0,100,100);
    auto clipped=sample();check(!WardrobeClip::Failed(),"clip runtime state");
    check(pixel(clipped,125,125)==0xffff,"root pixels preserved");check(pixel(clipped,175,175)==0xff0000,"child inside preserved");
    check(pixel(clipped,225,225)==0,"grandchild outside clipped");check(pixel(clipped,305,55)==0xff00,"unrelated layer preserved");
    WardrobeClip::SetVisible(false);auto hidden=sample();
    check(pixel(hidden,125,125)==0&&pixel(hidden,175,175)==0,"closing hides preview and name descendants before release");
    check(pixel(hidden,305,55)==0xff00,"closing preserves unrelated rendering");
    // Reproduce the hole in the old close path: native teardown detaches an
    // overlay while an animation still retains the child. The draw gate loses it.
    hr(method<HRESULT(__stdcall*)(void*,VARIANT)>(grandchild,0xfc)(grandchild,VARIANT{}),"native destructor detaches child");
    auto orphan=sample();check(pixel(orphan,225,225)==0xff,"old ancestry gate exposes detached child");
    hr(method<HRESULT(__stdcall*)(void*,VARIANT)>(grandchild,0xfc)(grandchild,unknown(child)),"restore test child");
    WardrobeClip::SetVisible(true);auto shown=sample();check(pixel(shown,175,175)==0xff0000,"show restores name child");
    unsigned enabled=99;hr(method<HRESULT(__stdcall*)(void*,int,unsigned*)>(device,0xcc)(device,152,&enabled),"plane state");check(enabled==0,"enable mask restored");
    for(unsigned i=0;i<4;i++){float plane[4]{};hr(method<HRESULT(__stdcall*)(void*,unsigned,float*)>(device,0xc4)(device,i,plane),"read plane");check(!std::memcmp(plane,originalPlanes[i],sizeof(plane)),"plane restored");}
    VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;
    hr(method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(root,0x90)(root,50,100,missing,missing),"move root");
    auto moved=sample();check(pixel(moved,75,125)==0xffff,"translated root");check(pixel(moved,125,175)==0xff0000,"translated child");check(pixel(moved,175,225)==0,"translated crop boundary");
    WardrobeClip::SetRoot(nullptr,0,0,0,0);auto restored=sample();check(pixel(restored,175,225)==0xff,"clearing root restores child rendering");
    // Production-like chain: window z=10 -> empty clip root z=100 -> native name z=5.
    auto purple=canvas(250,0xff330033),windowLayer=layer(purple,30,20,250,nullptr,10);
    for(int offset:{0x64,0xfc})hr(method<HRESULT(__stdcall*)(void*,VARIANT)>(root,offset)(root,unknown(windowLayer)),"root window parent");
    hr(method<HRESULT(__stdcall*)(void*,int)>(root,0xb4)(root,100),"root z");
    hr(method<HRESULT(__stdcall*)(void*,int)>(child,0xb4)(child,5),"name child z");
    hr(method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(root,0x90)(root,100,100,missing,missing),"root relative position");
    LONG absoluteX=0,absoluteY=0;
    hr(method<HRESULT(__stdcall*)(void*,LONG*)>(root,0x20)(root,&absoluteX),"root absolute x");
    hr(method<HRESULT(__stdcall*)(void*,LONG*)>(root,0x28)(root,&absoluteY),"root absolute y");
    printf("window origin: root absolute=%ld,%ld\n",absoluteX,absoluteY);
    check(absoluteX==130&&absoluteY==120,"origin contributes to get_x/get_y");
    void* camera=nullptr;hr(method<HRESULT(__stdcall*)(void*,void**)>(gr,0x5c)(gr,&camera),"camera");
    cameraX=120;cameraY=80;
    hr(method<HRESULT(__stdcall*)(void*,int,int,VARIANT,VARIANT)>(camera,0x90)(camera,cameraX,cameraY,missing,missing),"nonzero camera");
    WardrobeClip::SetRoot(root,0,0,100,100);auto cameraImage=sample();
    printf("camera pixels: root=%06x name=%06x outside=%06x unrelated=%06x\n",pixel(cameraImage,155,145),pixel(cameraImage,205,195),pixel(cameraImage,255,245),pixel(cameraImage,305,55));
    check(pixel(cameraImage,155,145)==0xffff,"camera translated root");
    check(pixel(cameraImage,205,195)==0xff0000,"name z=5 appears above window z=10 via overlay");
    check(pixel(cameraImage,255,245)==0x330033,"camera translated crop shows window underneath");
    check(pixel(cameraImage,305,55)==0xff00,"camera unrelated layer preserved");
    WardrobeClip::SetRoot(root,20,30,60,50);auto inset=sample();
    check(pixel(inset,155,145)==0x330033,"nonzero viewport top edge");
    check(pixel(inset,205,195)==0xff0000,"nonzero viewport inside");
    check(pixel(inset,205,210)==0x330033,"nonzero viewport bottom edge");
    std::vector<void*> parts;for(int i=0;i<32;i++)parts.push_back(layer(blue,10+i,10,20,root,20+i));
    setPlane=reinterpret_cast<SetPlane>(deviceTable[0xc0/4]);replace(&deviceTable[0xc0/4],reinterpret_cast<void*>(&CountPlane));
    planeWrites=0;sample();printf("clip plane writes for 3 frames / 32 extra avatar parts: %u\n",planeWrites);
    check(planeWrites<96,"consecutive avatar layers reuse clip planes");
    replace(&deviceTable[0xc0/4],reinterpret_cast<void*>(setPlane));
    for(void* part:parts)release(part);
    check(WardrobeClip::RetireTree()==3,"retire exactly the preview tree");
    WardrobeClip::SetRoot(nullptr,0,0,0,0);
    auto retired=sample(); // Keep all three COM references alive after closing.
    for(auto color:retired){color&=0xffffff;check(color!=0xff&&color!=0xffff&&color!=0xff0000,"retained avatar/name layers never reappear after gate removal");}
    check(pixel(retired,155,145)==0x330033&&pixel(retired,305,55)==0xff00,"retirement preserves window and field siblings");
    for(void* item:{grandchild,child,root})check(static_cast<IUnknown*>(item)->Release()==0,"retired layer actually destroyed");
    auto registry=*reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(sibling)+0x2c);
    auto baseline=*reinterpret_cast<unsigned*>(registry+0x14);
    for(unsigned cycle=0;cycle<50;cycle++){
        auto parent=layer(cyan,100,100,100,nullptr),part=layer(red,0,0,100,parent);
        WardrobeClip::SetRoot(parent,0,0,100,100);
        // A retained attachment can refer back to its parent. Teardown must break
        // both origin and overlay links, not just hide and accumulate dead trees.
        hr(method<HRESULT(__stdcall*)(void*,VARIANT)>(parent,0x64)(parent,unknown(part)),"temporary origin cycle");
        check(WardrobeClip::RetireTree()==2,"repeat close collects both layers");
        WardrobeClip::SetRoot(nullptr,0,0,0,0);
        check(static_cast<IUnknown*>(part)->Release()==0,"release repeated child");
        check(static_cast<IUnknown*>(parent)->Release()==0,"release repeated parent");
        check(*reinterpret_cast<unsigned*>(registry+0x14)==baseline,"no native layer accumulation across reopen/close");
    }
    release(camera);release(windowLayer);release(purple);
    WardrobeClip::Uninstall();replace(&deviceTable[15],originalPresent);
    for(void* p:{sibling,cyan,red,blue,green})release(p);
    hr(method<HRESULT(__stdcall*)(void*)>(gr,0x10)(gr),"graphics uninitialize");release(gr);DestroyWindow(window);
    // Shut PCOM down like BeiDou.exe does (it imports PcTermModule). Without this, PCOM's
    // DLL_PROCESS_DETACH walks already freed module state and intermittently faults at exit.
    auto terminate=reinterpret_cast<void(__cdecl*)()>(GetProcAddress(pc,"PcTermModule"));check(terminate!=nullptr,"PCOM terminate");terminate();
    CoUninitialize();
    puts("PASS: native clipping, detached/retained-child teardown, zero final references, 50 close cycles without layer growth and GPU state restoration");return 0;
}
