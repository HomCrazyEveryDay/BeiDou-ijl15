#include "stdafx.h"
#include "WardrobeInput.h"
#include "Memory.h"
#include "CrashReporter.h"
#include <atomic>

namespace {
std::atomic<bool> locked{false};
bool previewEnabled=false,foreground=false;
unsigned hooks=0;
int directionX=0,directionY=0;
WardrobeInput::ReleaseGate gate;
thread_local unsigned previewDepth=0;
using NativePressed=int(__fastcall*)(void*,void*,int);
NativePressed nativePressed=reinterpret_cast<NativePressed>(0x0059A25A);
using UpdateActive=void(__fastcall*)(void*,void*);
UpdateActive updateActive=reinterpret_cast<UpdateActive>(0x009B1928);
using HasFocus=int(__fastcall*)(void*,void*);
HasFocus hasFocus=reinterpret_cast<HasFocus>(0x009E06C5);
int __fastcall PreviewFocus(void* receiver,void* edx) {
    // CVecCtrlUserPreview::Work asks whether CWndMan itself has focus. The wardrobe
    // owns focus for its IME/UI; only this synchronous private controller gets root focus.
    if(previewDepth && receiver==*reinterpret_cast<void**>(0x00BEC20C))return 1;
    return hasFocus(receiver,edx);
}
void __fastcall UpdatePhysics(void* vector,void* edx) {
    auto user=*reinterpret_cast<unsigned char**>(0x00BEBF98);
    // Freeze only the local player's controller, never CUserLocal::Update: damage,
    // buffs, network events and the rest of the field continue to update normally.
    if(locked.load(std::memory_order_relaxed) && user
        && *reinterpret_cast<void**>(user+0x11a4)==static_cast<unsigned char*>(vector)+12)return;
    updateActive(vector,edx);
}
bool Physical(unsigned key){return (GetAsyncKeyState(key)&0x8000)!=0;}
bool Foreground(){DWORD process=0;GetWindowThreadProcessId(GetForegroundWindow(),&process);return process==GetCurrentProcessId();}
void HoldCurrent(){for(unsigned key=1;key<256;key++)if(Physical(key))gate.Hold(key);}

bool MappedDirection(unsigned key) {
    int action=key==VK_UP?28:key==VK_DOWN?29:key==VK_LEFT?30:key==VK_RIGHT?31:0;
    if(!action)return false;
    auto map=*reinterpret_cast<unsigned char**>(0x00BED5A0);if(!map)return false;
    for(unsigned scan=1;scan<90;scan++) {
        auto binding=map+4+scan*5;int value=0;std::memcpy(&value,binding+1,4);
        if(binding[0]!=4 || (value!=action && value!=action+79))continue;
        unsigned vk=MapVirtualKeyW(scan,MAPVK_VSC_TO_VK_EX);
        if(vk && gate.Read(vk,Physical(vk)))return true;
    }
    return false;
}
int __fastcall KeyPressed(void* input,void* edx,int key) {
    if(key<0 || key>=256)return 0;
    if(previewDepth)return WardrobeInput::Pressed(unsigned(key))?1:0;
    if(locked.load(std::memory_order_relaxed))return 0;
    if(gate.Held(unsigned(key)) && gate.Suppressed(unsigned(key),Physical(unsigned(key))))return 0;
    return nativePressed(input,edx,key);
}
}

bool WardrobeInput::Install() {
    if(hooks)return hooks==7;
    // This consumer is used by the native local-user and cash-shop movement controllers,
    // including the GetKeyState fallback when DirectInput is disabled. Do not clear DI's
    // shared buffer: CInputSystem also uses it to generate UI key/IME events.
    const unsigned char signature[]={0x8b,0x44,0x24,0x04,0x33,0xd2,0x3b,0xc2};
    if(std::memcmp(reinterpret_cast<void*>(0x0059A25A),signature,sizeof(signature)))return false;
    const unsigned char update[]={0x56,0x8b,0xf1,0x8b,0x06,0x57,0xff,0x50,0x24};
    if(std::memcmp(reinterpret_cast<void*>(0x009B1928),update,sizeof(update)))return false;
    const unsigned char focus[]={0x8b,0x15,0x0c,0xc2,0xbe,0,0x8b,0xc1,0x83,0xc1,0x04};
    if(std::memcmp(reinterpret_cast<void*>(0x009E06C5),focus,sizeof(focus)))return false;
    if(!Memory::SetHook(true,reinterpret_cast<void**>(&nativePressed),reinterpret_cast<void*>(&KeyPressed)))return false;
    hooks|=1;
    if(!Memory::SetHook(true,reinterpret_cast<void**>(&updateActive),reinterpret_cast<void*>(&UpdatePhysics))) {
        Uninstall();return false;
    }
    hooks|=2;
    if(!Memory::SetHook(true,reinterpret_cast<void**>(&hasFocus),reinterpret_cast<void*>(&PreviewFocus))) {
        Uninstall();return false;
    }
    hooks|=4;return true;
}
void WardrobeInput::Uninstall(){
    if((hooks&4)&&Memory::SetHook(false,reinterpret_cast<void**>(&hasFocus),reinterpret_cast<void*>(&PreviewFocus)))hooks&=~4u;
    if((hooks&2)&&Memory::SetHook(false,reinterpret_cast<void**>(&updateActive),reinterpret_cast<void*>(&UpdatePhysics)))hooks&=~2u;
    if((hooks&1)&&Memory::SetHook(false,reinterpret_cast<void**>(&nativePressed),reinterpret_cast<void*>(&KeyPressed)))hooks&=~1u;
    if(hooks)CrashReporter::RecordEvent("wardrobe.input","detach_failed hooks=%u",hooks);
}
void WardrobeInput::Lock(){
    HoldCurrent();previewEnabled=false;foreground=Foreground();
    auto user=*reinterpret_cast<unsigned char**>(0x00BEBF98);
    auto vector=user?*reinterpret_cast<unsigned char**>(user+0x11a4):nullptr;
    if(vector) {
        vector-=12;
        // BeginUpdateActive normally copies the secure m_ap to m_apl. Collapse the
        // interpolation interval at the boundary, so a held movement key cannot drift.
        std::memcpy(vector+0x80,vector+0x20,0x60);
        *reinterpret_cast<int*>(vector+0x168)=0;
    }
    locked.store(true,std::memory_order_relaxed);
}
void WardrobeInput::Unlock(){HoldCurrent();previewEnabled=false;locked.store(false,std::memory_order_relaxed);}
bool WardrobeInput::Locked(){return locked.load(std::memory_order_relaxed);}
bool WardrobeInput::IsForeground(){return foreground;}
void WardrobeInput::PreviewEnabled(bool enabled){if(previewEnabled!=enabled)HoldCurrent();previewEnabled=enabled;if(!enabled)directionX=directionY=0;}
void WardrobeInput::UpdateFocus(){
    if(!Locked()&&!gate.Pending())return;
    bool next=Foreground();if(next!=foreground)HoldCurrent();foreground=next;
    for(unsigned key=1;key<256;key++)if(gate.Held(key))gate.Suppressed(key,Physical(key));
}
bool WardrobeInput::Pressed(unsigned key){
    bool command=key==VK_LEFT?directionX<0:key==VK_RIGHT?directionX>0:key==VK_UP?directionY<0:key==VK_DOWN?directionY>0:false;
    return Locked() && previewEnabled && foreground && (command || gate.Read(key,Physical(key)) || MappedDirection(key));
}
void WardrobeInput::PreviewDirection(int x,int y){directionX=x;directionY=y;}
bool WardrobeInput::AcceptPreviewKey(unsigned key,unsigned flags){
    return !(flags&0xc0000000u) && Locked() && previewEnabled && foreground
        && key<256 && !gate.Suppressed(key,Physical(key));
}
bool WardrobeInput::ConsumeAfterClose(unsigned key,unsigned flags){
    bool down=(flags&0x80000000u)==0;
    return down && gate.Suppressed(key,Physical(key));
}
WardrobeInput::PreviewScope::PreviewScope(){++previewDepth;}
WardrobeInput::PreviewScope::~PreviewScope(){--previewDepth;}
WardrobeInput::FieldScope::FieldScope(){previous=previewDepth;previewDepth=0;}
WardrobeInput::FieldScope::~FieldScope(){previewDepth=previous;}
