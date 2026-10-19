#include "stdafx.h"
#include "WardrobeClip.h"
#include "Memory.h"
#include <cstring>
#include <vector>
#include <oleauto.h>

namespace {
void* root=nullptr;void* layerTable=nullptr;
void* checkedDevice=nullptr;bool deviceSupported=false;
int x=0,y=0,width=0,height=0;unsigned hooks=0;bool failed=false,visible=true;
using Render=void(__fastcall*)(void*,void*,void*);
Render render=nullptr;
using RenderFrame=HRESULT(__stdcall*)(void*);
RenderFrame renderFrame=nullptr;
using EnterLock=void(__thiscall*)(void*);
EnterLock enterLock=nullptr;
thread_local unsigned frameDepth=0;
thread_local WardrobeClip::DeviceState active;
HRESULT __stdcall DrawFrame(void* graphics){
    active.Restore();++frameDepth;
    HRESULT result=E_FAIL;
    __try {result=renderFrame(graphics);}
    __finally {active.Restore();--frameDepth;}
    return result;
}
bool Owned(void* layer) {
    // Verified against this exact Gr2D_DX8 build: get_overlay (+A45D) returns
    // the IWzGr2DLayer pointer at +30. Rendering holds the engine's layer lock.
    // No COM allocation, cached identity, or retained references on the frame path.
    for(unsigned i=0;layer && i<32;i++) {
        if(layer==root)return true;
        if(*static_cast<void**>(layer)!=layerTable)return false;
        layer=*reinterpret_cast<void**>(static_cast<unsigned char*>(layer)+0x30);
    }
    return false;
}
struct LayerReferences {
    std::vector<void*> values;
    ~LayerReferences(){for(auto layer:values)DreamCanvas::Release(layer);}
    void Keep(void* layer){values.push_back(layer);static_cast<IUnknown*>(layer)->AddRef();}
};
void CollectTree(LayerReferences& layers) {
    // This registry and lock are verified for the same Gr2D build as DrawLayer.
    // +2C is the layer registry; its ZList data nodes hold layer+0C. Traverse
    // under the engine's own recursive lock, and retain before leaving the lock.
    // There is no registry walk, allocation or extra reference on the frame path.
    auto registry=*reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(root)+0x2c);
    if(!registry || !enterLock)throw E_UNEXPECTED;
    auto lock=reinterpret_cast<volatile LONG*>(registry+4);
    enterLock(const_cast<LONG*>(lock));
    __try {
        auto node=*reinterpret_cast<unsigned char**>(registry+0x18);
        unsigned visited=0;
        while(node) {
            if(++visited>65536)throw E_UNEXPECTED;
            auto layer=*reinterpret_cast<unsigned char**>(node)-12;
            if(*reinterpret_cast<void**>(layer)!=layerTable)throw E_NOINTERFACE;
            if(Owned(layer))layers.Keep(layer);
            auto next=*reinterpret_cast<unsigned char**>(node-12);
            node=next?next+16:nullptr;
        }
    } __finally {if(InterlockedDecrement(lock+1)==0)InterlockedExchange(lock,0);}
}
void RenderClipped(void* layer,void* edx,void* context,float left,float top) {
    WardrobeClip::DeviceState saved;
    // POD-only SEH finally: also restore device state if native rendering throws.
    __try {
        if(saved.Begin(*static_cast<void**>(context),left,top,left+width,top+height))render(layer,edx,context);
        else failed=true;
    } __finally {saved.Restore();}
}
void __fastcall DrawLayer(void* layer,void* edx,void* context) {
    if(!root || !Owned(layer)){active.Restore();render(layer,edx,context);return;}
    if(!visible){active.Restore();return;}
    auto device=*static_cast<void**>(context);
    // Consecutive avatar/name/effect layers share one clip state. End it before
    // unrelated UI/cursor layers and unconditionally when the render frame ends.
    if(frameDepth && active.captured && active.device==device){render(layer,edx,context);return;}
    active.Restore();
    if(device!=checkedDevice){checkedDevice=device;deviceSupported=WardrobeClip::SupportsPlanes(device);}
    if(!deviceSupported){failed=true;return;}
    LONG left=0,top=0;
    using Get=HRESULT(__stdcall*)(void*,LONG*);
    if(FAILED(DreamCanvas::Method<Get>(root,0x20)(root,&left)) || FAILED(DreamCanvas::Method<Get>(root,0x28)(root,&top))){failed=true;return;}
    if(!frameDepth){RenderClipped(layer,edx,context,float(left+x),float(top+y));return;}
    if(!active.Begin(device,float(left+x),float(top+y),float(left+x+width),float(top+y+height))){
        active.Restore();failed=true;return;
    }
    render(layer,edx,context);
}
}
bool WardrobeClip::Install(void* graphics) {
    if(hooks)return hooks==3;
    if(!graphics)return false;
    auto module=reinterpret_cast<unsigned char*>(GetModuleHandleW(L"Gr2D_DX8.dll"));if(!module)return false;
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(module);if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
    auto pe=reinterpret_cast<IMAGE_NT_HEADERS*>(module+dos->e_lfanew);
    if(pe->Signature!=IMAGE_NT_SIGNATURE || pe->FileHeader.TimeDateStamp!=0x4b7c13fd || pe->OptionalHeader.SizeOfImage!=0x40000)return false;
    const BYTE lockEntry[]={0x56,0x8b,0xf1,0xff,0x15};
    if(std::memcmp(module+0x188a,lockEntry,sizeof(lockEntry)))return false;
    enterLock=reinterpret_cast<EnterLock>(module+0x188a);
    auto entry=module+0xda7c;
    const BYTE tail[]={0x81,0xec,0x28,0x01,0,0,0x53,0x56,0x57,0x8b,0xd9};
    if(entry[0]!=0xb8 || *reinterpret_cast<DWORD*>(entry+1)!=DWORD(module+0x2ba47)
        || entry[5]!=0xe8 || std::memcmp(entry+10,tail,sizeof(tail)))return false;
    render=reinterpret_cast<Render>(entry);
    auto frame=DreamCanvas::Method<RenderFrame>(graphics,0x1c);
    if(reinterpret_cast<unsigned char*>(frame)<module || reinterpret_cast<unsigned char*>(frame)>=module+pe->OptionalHeader.SizeOfImage)return false;
    renderFrame=frame;
    if(!Memory::SetHook(true,reinterpret_cast<void**>(&renderFrame),reinterpret_cast<void*>(&DrawFrame)))return false;
    hooks=1;
    if(!Memory::SetHook(true,reinterpret_cast<void**>(&render),reinterpret_cast<void*>(&DrawLayer))){Uninstall();return false;}
    hooks=3;return true;
}
void WardrobeClip::SetRoot(void* layer,int left,int top,int w,int h){active.Restore();root=layer;layerTable=layer?*static_cast<void**>(layer):nullptr;x=left;y=top;width=w;height=h;failed=false;visible=true;checkedDevice=nullptr;}
void WardrobeClip::SetVisible(bool value){active.Restore();visible=value;}
unsigned WardrobeClip::RetireTree(){
    SetVisible(false);if(!root)return 0;
    LayerReferences layers;CollectTree(layers);
    // Hide EVERY layer before detaching any parent: a child's native visibility
    // remains false after it loses the ancestry used by our clipping callback.
    using Put=HRESULT(__stdcall*)(void*,int);
    for(auto layer:layers.values) {
        HRESULT hr=DreamCanvas::Method<Put>(layer,0x11c)(layer,0);if(FAILED(hr))throw hr;
    }
    using Link=HRESULT(__stdcall*)(void*,VARIANT);
    for(auto layer:layers.values)for(unsigned slot:{0x64u,0xfcu}) {
        HRESULT hr=DreamCanvas::Method<Link>(layer,slot)(layer,VARIANT{});if(FAILED(hr))throw hr;
    }
    return static_cast<unsigned>(layers.values.size());
}
void WardrobeClip::Uninstall(){
    active.Restore();root=nullptr;
    if((hooks&2) && Memory::SetHook(false,reinterpret_cast<void**>(&render),reinterpret_cast<void*>(&DrawLayer)))hooks&=~2u;
    if((hooks&1) && Memory::SetHook(false,reinterpret_cast<void**>(&renderFrame),reinterpret_cast<void*>(&DrawFrame)))hooks&=~1u;
}
bool WardrobeClip::Failed(){return failed;}
