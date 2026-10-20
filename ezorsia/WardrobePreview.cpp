#include "stdafx.h"
#include "WardrobePreview.h"
#include "WardrobeInput.h"
#include "WardrobeLayout.h"
#include "WardrobeClip.h"
#include "MixedDyeResources.h"
#include "CrashReporter.h"

namespace {
using namespace MixedDyeResources;
struct Preview {
    DWORD space[2]{},user[2]{};
    void* overlay=nullptr;void* clipLayer=nullptr;
    unsigned char look[0x1c5]{};
    DWORD thread=0;
    int scene=0;
    bool visible=false,poisoned=false;
};
Preview p;
using ReleaseSpace=void(__thiscall*)(void*,int);
using ReleaseUser=void(__thiscall*)(void*);
using SetAvatarLook=void(__thiscall*)(void*,const void*);
ReleaseSpace releaseSpace=reinterpret_cast<ReleaseSpace>(0x00473FCC);
ReleaseUser releaseUser=reinterpret_cast<ReleaseUser>(0x00473E05);
SetAvatarLook setAvatarLook=reinterpret_cast<SetAvatarLook>(0x004515A1);
ReleaseUser refreshRings=reinterpret_cast<ReleaseUser>(0x0092E964);
void ApplyLook();
unsigned hooks=0;
thread_local unsigned depth=0;
thread_local unsigned externalDepth=0;
thread_local void* fieldSpace=nullptr;
thread_local void* fieldPreview=nullptr;
void*& SpaceGlobal(){return *reinterpret_cast<void**>(0x00BEBFA0);}
void*& UserGlobal(){return *reinterpret_cast<void**>(0x00BED5A4);}
unsigned char* User(){return reinterpret_cast<unsigned char*>(p.user[1]);}
unsigned char* Vector(){auto user=User();auto vector=user?*reinterpret_cast<unsigned char**>(user+0x11a4):nullptr;return vector?vector-12:nullptr;}
void AddRef(void* v){if(v)static_cast<IUnknown*>(v)->AddRef();}
VARIANT Unknown(void* v){VARIANT a{};a.vt=VT_UNKNOWN;a.punkVal=static_cast<IUnknown*>(v);return a;}
Factory FactoryFn(){return reinterpret_cast<Factory>(GetProcAddress(GetModuleHandleW(L"PCOM.dll"),"PcCreateObject"));}
Value Resource(const wchar_t* path) {
    auto manager=*reinterpret_cast<void**>(0x00BF14E8);if(!manager)throw E_POINTER;
    Value out;VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;
    BSTR name=SysAllocString(path);if(!name)throw E_OUTOFMEMORY;
    HRESULT hr=Method<HRESULT(__stdcall*)(void*,BSTR,VARIANT,VARIANT,VARIANT*)>(manager,0x1c)(manager,name,missing,missing,&out.v);
    SysFreeString(name);Check(hr);return out;
}

// Kept in a POD-only wrapper so MSVC runs this finally block for BOTH native C++
// exceptions and SEH faults. Never cache the field singleton across calls/map changes.
void NativeCall(void(*operation)()) {
    if(p.thread && p.thread!=GetCurrentThreadId())throw E_UNEXPECTED;
    if(depth)throw E_UNEXPECTED;
    void* previousSpace=SpaceGlobal();void* previousUser=UserGlobal();
    fieldSpace=previousSpace;fieldPreview=previousUser;++depth;
    if(p.space[1])SpaceGlobal()=reinterpret_cast<void*>(p.space[1]);
    if(p.user[1])UserGlobal()=User();
    __try {operation();}
    __finally {SpaceGlobal()=fieldSpace;UserGlobal()=fieldPreview;--depth;fieldSpace=fieldPreview=nullptr;}
}

using AvatarInit=void(__fastcall*)(void*,void*,void*,int,int,void*,int);
AvatarInit avatarInit=reinterpret_cast<AvatarInit>(0x004502F8);
void __fastcall InitAvatar(void* avatar,void* edx,void* origin,int x,int y,void* overlay,int z) {
    if(!p.poisoned && User() && avatar==User()+0x88) {
        // These COM arguments are passed by value and consumed by the native callee.
        DreamCanvas::Release(overlay);overlay=p.clipLayer;AddRef(overlay);z=100;
    }
    avatarInit(avatar,edx,origin,x,y,overlay,z);
}
using UserZ=void(__fastcall*)(void*,void*);
UserZ userZ=reinterpret_cast<UserZ>(0x0092FD16);
void __fastcall SetUserZ(void* user,void* edx) {
    if(!p.poisoned && User() && user==User())reinterpret_cast<void(__thiscall*)(void*,int)>(0x00452195)(User()+0x88,100);
    else userZ(user,edx);
}

// CVecCtrl::_GetSnapshot, COM-this = controller+0C, stdcall ret 34.
// Preserve the shop's native interpolation at render time. Replacing controller+1C
// with a plain vector bypasses the interpolation and freezes positions between ticks.
using Snapshot=HRESULT(__stdcall*)(void*,LONG*,LONG*,LONG*,LONG*,LONG*,LONG*,double*,double*,VARIANT);
Snapshot snapshot=reinterpret_cast<Snapshot>(0x009B6205);
HRESULT __stdcall DisplaySnapshot(void* self,LONG* x,LONG* y,LONG* rx,LONG* ry,LONG* a,LONG* b,double* c,double* d,VARIANT time){
    HRESULT result=snapshot(self,x,y,rx,ry,a,b,c,d,time);
    if(FAILED(result) || p.poisoned || !p.overlay || !User() || self!=*reinterpret_cast<void**>(User()+0x11a4))return result;
    LONG left=0,top=0;
    using Get=HRESULT(__stdcall*)(void*,LONG*);
    result=Method<Get>(p.overlay,0x20)(p.overlay,&left);if(FAILED(result))return result;
    result=Method<Get>(p.overlay,0x28)(p.overlay,&top);if(FAILED(result))return result;
    // Original shop artwork starts at world (-376,-260) and fills the viewport 1:1. Translate
    // all positional outputs just for this preview; native physics and other actors stay untouched.
    left+=p.visible?WardrobeLayout::Preview.x-WardrobeLayout::PreviewWorldX:-32000;
    top+=p.visible?WardrobeLayout::Preview.y-WardrobeLayout::PreviewWorldY:-32000;
    if(x)*x+=left;if(y)*y+=top;if(rx)*rx+=left;if(ry)*ry+=top;
    return result;
}
void BuildNative() {
    auto floorValue=Resource(L"UI/CashShopPreview.img/foothold");auto floor=Query(floorValue.v,PropertyIID());
    auto climbValue=Resource(L"UI/CashShopPreview.img/ladderRope");auto climb=Query(climbValue.v,PropertyIID());
    if(!floor.p || !climb.p)throw E_NOINTERFACE;
    reinterpret_cast<void(__thiscall*)(void*)>(0x004B1F77)(p.space);
    if(!p.space[1])throw E_OUTOFMEMORY;
    AddRef(floor.p);void* climbing=climb.p;AddRef(climbing);
    reinterpret_cast<void(__thiscall*)(void*,void*,void*,void*)>(0x00A43E7B)
        (reinterpret_cast<void*>(p.space[1]),floor.p,climbing,nullptr);
    reinterpret_cast<void(__thiscall*)(void*)>(0x00A45B8C)(reinterpret_cast<void*>(p.space[1]));
    reinterpret_cast<void(__thiscall*)(void*)>(0x004B1FAA)(p.user);
    if(!p.user[1])throw E_OUTOFMEMORY;
    reinterpret_cast<void(__thiscall*)(void*)>(0x009785BD)(User());
    ApplyLook();
}
void CreateClipLayer() {
    auto gr=*reinterpret_cast<void**>(0x00BF14EC);if(!gr)throw E_POINTER;
    if(!WardrobeClip::Install(gr))throw E_NOINTERFACE;
    VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;
    // A private parent makes ownership explicit for every native avatar/name/effect child.
    Check(Method<HRESULT(__stdcall*)(void*,int,int,unsigned,unsigned,int,VARIANT,VARIANT,void**)>(gr,0x64)
        (gr,0,0,WardrobeLayout::Preview.w,WardrobeLayout::Preview.h,100,Integer(0),missing,&p.clipLayer));
    Check(Method<HRESULT(__stdcall*)(void*,VARIANT)>(p.clipLayer,0x64)(p.clipLayer,Unknown(p.overlay)));
    Check(Method<HRESULT(__stdcall*)(void*,VARIANT)>(p.clipLayer,0xfc)(p.clipLayer,Unknown(p.overlay)));
    Check(Method<HRESULT(__stdcall*)(void*,unsigned)>(p.clipLayer,0xe0)(p.clipLayer,0xffffffff));
    Check(Method<HRESULT(__stdcall*)(void*,int)>(p.clipLayer,0x11c)(p.clipLayer,1));
    auto r=WardrobeLayout::Preview;WardrobeClip::SetRoot(p.clipLayer,r.x,r.y,r.w,r.h);
}
void DestroyNative() {
    // CSWnd::OnDestroy (004ABD6E): release CSpace2D first, then CUserPreview.
    // Retire native layers first. The draw-hook visibility gate alone stops
    // protecting a layer as soon as the native destructor detaches its overlay.
    unsigned retired=WardrobeClip::RetireTree();
    if(p.space[1])releaseSpace(p.space,0);
    p.space[0]=p.space[1]=0;
    bool hadUser=p.user[1]!=0;
    if(hadUser)releaseUser(p.user);
    // CUserPreview::~CUserPreview clears BED5A4 at 004B24EC. Observe that native
    // completion before NativeCall restores the field singleton; never read freed memory.
    if(hadUser)CrashReporter::RecordEvent("wardrobe.preview","retired_layers=%u user_destroyed=%d",retired,UserGlobal()==nullptr);
    p.user[0]=p.user[1]=0;
}
void ApplyLook(){
    setAvatarLook(User()+0x88,p.look);
    // CUser::UpdateRing scans the four native ring slots and rebuilds the name
    // layer on a nameTag change. CAvatar::SetAvatarLook alone never does this.
    refreshRings(User());
}
void TickNative(){WardrobeInput::PreviewScope input;reinterpret_cast<void(__thiscall*)(void*)>(0x00978906)(User());}
void AttackNative(){
    if(*reinterpret_cast<int*>(User()+0x2b0c))return;
    // Match CSWnd::OnKey 4AD375..4AD395: riding previews cannot attack.
    int ride=*reinterpret_cast<int*>(User()+0x544)/10000;
    if(ride==190||ride==193)return;
    int base=0,sticker=0,action=0;std::memcpy(&base,p.look+0x45,4);std::memcpy(&sticker,p.look+0x15,4);
    // The audited 083 selector (4AD450..4AD51B) reads only these two CSWnd fields.
    // Reuse its cash-weapon compatibility and action lookup without constructing a shop UI.
    unsigned char selection[0x2b8]{};int selected=sticker?sticker:base;
    std::memcpy(selection+0xd9,&base,4);std::memcpy(selection+0x2b4,&selected,4);
    int wanted=reinterpret_cast<int(__cdecl*)(int)>(0x00460AA0)(base);
    int weapon=reinterpret_cast<int(__thiscall*)(void*,int,int*)>(0x004AD450)(selection,wanted,&action);
    if(!weapon)return;
    int type=reinterpret_cast<int(__cdecl*)(int)>(0x00460AA0)(weapon);
    // 083 CSWnd::OnKey at 4AD3BE routes bows/crossbows through DoShootAttack;
    // claw, gun and knuckle use DoMeleeAttack's native action selection.
    auto address=type==45 || type==46?0x00978D9C:0x00978C09;
    reinterpret_cast<void(__thiscall*)(void*,int,int)>(address)(User(),weapon,(type==45 || type==46)?0:action);
}
}

bool WardrobePreview::Install() {
    if(hooks)return hooks==7;
    const unsigned char init[]={0xb8,0x2c,0xc0,0xa7,0,0xe8},z[]={0x8b,0x81,0xa4,0x11,0,0};
    if(std::memcmp(reinterpret_cast<void*>(0x004502F8),init,sizeof(init)) || std::memcmp(reinterpret_cast<void*>(0x0092FD16),z,sizeof(z)))return false;
    if(*reinterpret_cast<DWORD*>(0x00AF27F0)!=0x00978906 || *reinterpret_cast<DWORD*>(0x00B3EBD8)!=0x009CF76F)return false;
    const unsigned char position[]={0x55,0x8b,0xec,0x83,0xec,0x10,0x53,0x56};
    if(std::memcmp(reinterpret_cast<void*>(0x009B6205),position,sizeof(position)))return false;
    const unsigned char rings[]={0xb8,0x74,0xb7,0xad,0,0xe8};
    if(std::memcmp(reinterpret_cast<void*>(0x0092E964),rings,sizeof(rings)))return false;
    if(!Memory::SetHook(true,reinterpret_cast<void**>(&avatarInit),reinterpret_cast<void*>(&InitAvatar)))return false;
    hooks|=1;
    if(!Memory::SetHook(true,reinterpret_cast<void**>(&userZ),reinterpret_cast<void*>(&SetUserZ))) {
        Uninstall();return false;
    }
    hooks|=2;
    if(!Memory::SetHook(true,reinterpret_cast<void**>(&snapshot),reinterpret_cast<void*>(&DisplaySnapshot))){Uninstall();return false;}
    hooks|=4;return true;
}
void WardrobePreview::Uninstall(){
    if((hooks&4)&&Memory::SetHook(false,reinterpret_cast<void**>(&snapshot),reinterpret_cast<void*>(&DisplaySnapshot)))hooks&=~4u;
    if((hooks&2)&&Memory::SetHook(false,reinterpret_cast<void**>(&userZ),reinterpret_cast<void*>(&SetUserZ)))hooks&=~2u;
    if((hooks&1)&&Memory::SetHook(false,reinterpret_cast<void**>(&avatarInit),reinterpret_cast<void*>(&InitAvatar)))hooks&=~1u;
    if(hooks)CrashReporter::RecordEvent("wardrobe.preview","detach_failed hooks=%u",hooks);
}
bool WardrobePreview::Ready(){return !p.poisoned && p.user[1]!=0;}
bool WardrobePreview::Busy(){return depth!=0;}
void WardrobePreview::Create(void* layer,const void* look){
    if(Ready() || p.poisoned)throw E_UNEXPECTED;p.thread=GetCurrentThreadId();p.overlay=layer;AddRef(layer);
    std::memcpy(p.look,look,sizeof(p.look));p.visible=true;
    try {CreateClipLayer();NativeCall(&BuildNative);}catch(...){Dispose();throw;}
}
void WardrobePreview::Dispose(){
    p.visible=false;WardrobeInput::PreviewEnabled(false);WardrobeInput::PreviewDirection(0,0);
    WardrobeClip::SetVisible(false);
    // A failed destructor is quarantined with its hidden parent still owned.
    // Never expose orphaned layers by removing their clipping/visibility root.
    if(p.poisoned)return;
    try {if(p.user[1] || p.space[1])NativeCall(&DestroyNative);else WardrobeClip::RetireTree();}
    catch(...){p.poisoned=true;CrashReporter::RecordEvent("wardrobe.preview","dispose_failed_quarantined");return;}
    WardrobeClip::SetRoot(nullptr,0,0,0,0);DreamCanvas::Release(p.clipLayer);p.clipLayer=nullptr;
    DreamCanvas::Release(p.overlay);p.overlay=nullptr;p.visible=false;
}
void WardrobePreview::SetLook(const void* look){std::memcpy(p.look,look,sizeof(p.look));if(Ready())NativeCall(&ApplyLook);}
void WardrobePreview::Update(bool visible,bool interactive){
    if(WardrobeClip::Failed())throw E_FAIL;
    if(!Ready())return;p.visible=visible;WardrobeClip::SetVisible(visible);WardrobeInput::PreviewEnabled(visible&&interactive);
    if(visible)NativeCall(&TickNative);
}
void WardrobePreview::SetScene(int scene){
    if(scene<0 || scene>=int(WardrobeLayout::Scenes.size()) || scene==p.scene)return;
    p.scene=scene;
}
int WardrobePreview::Scene(){return p.scene;}
void WardrobePreview::Jump(){if(Ready() && !*reinterpret_cast<int*>(User()+0x2b0c))*reinterpret_cast<int*>(Vector()+0x168)=1;}
void WardrobePreview::Attack(){if(Ready())NativeCall(&AttackNative);}
void WardrobePreview::Stop(){WardrobeInput::PreviewDirection(0,0);}
void WardrobePreview::Key(unsigned key,unsigned flags){
    if(!WardrobeInput::AcceptPreviewKey(key,flags) || !Ready())return;
    if(key>=VK_LEFT&&key<=VK_DOWN)Stop();
    unsigned scan=WardrobeInput::NormalizeScan((flags>>16)&255);if(scan>=90)return;
    auto mapping=*reinterpret_cast<unsigned char**>(0x00BED5A0);if(!mapping)return;
    auto binding=mapping+4+scan*5;int action=0;std::memcpy(&action,binding+1,4);
    if(binding[0]==4 && ((action>=28&&action<=31)||(action>=107&&action<=110)))Stop();
    if(binding[0]==5 && action==53)Jump();
    if(binding[0]==5 && action==52)Attack();
}
WardrobePreview::ExternalScope::ExternalScope(){
    if(depth){entered=true;if(externalDepth++==0){physics=SpaceGlobal();preview=UserGlobal();SpaceGlobal()=fieldSpace;UserGlobal()=fieldPreview;switched=true;}}
}
WardrobePreview::ExternalScope::~ExternalScope(){
    if(entered)--externalDepth;
    if(switched){fieldSpace=SpaceGlobal();fieldPreview=UserGlobal();SpaceGlobal()=physics;UserGlobal()=preview;}
}
