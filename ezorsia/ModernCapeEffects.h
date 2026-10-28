#pragma once
#include "MixedDyeResources.h"
#include "Memory.h"
#include "CrashReporter.h"
#include "AvatarActionIds.h"
#include <array>

// v83 CAvatar::Update has no modern action-based ItemEff pass. Render the original
// animation on a separate native layer, so delays and action variants do not
// get reduced to the number/timing of the avatar's equipment frames.
namespace ModernCapeEffects {
using namespace MixedDyeResources;
using AvatarCall=void(__thiscall*)(void*);
using ActionCall=int(__thiscall*)(void*,int*);
static auto update=reinterpret_cast<AvatarCall>(0x004522A6);
static auto destroy=reinterpret_cast<AvatarCall>(0x0045011C);
static auto actionFor=reinterpret_cast<ActionCall>(0x00451E4C);
using DestroyCallback=void(*)(void*);
inline DestroyCallback& OnAvatarDestroyed(){static DestroyCallback callback=nullptr;return callback;}
inline DestroyCallback& OnSetEffectsDestroyed(){static DestroyCallback callback=nullptr;return callback;}
using SetEffectsCallback=void(*)(void*,void*,void*,const std::wstring&,int);
inline SetEffectsCallback& UpdateSetEffects(){static SetEffectsCallback callback=nullptr;return callback;}
using HiddenCallback=bool(*)(void*,int);
inline HiddenCallback& HiddenByChair(){static HiddenCallback callback=nullptr;return callback;}
inline Value Resource(const std::wstring& path,void* manager=nullptr) {
    if(!manager)manager=*reinterpret_cast<void**>(0x00BF14E8);if(!manager)throw E_POINTER;
    Value out;VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;
    BSTR name=SysAllocString(path.c_str());if(!name)throw E_OUTOFMEMORY;
    auto hr=Method<HRESULT(__stdcall*)(void*,BSTR,VARIANT,VARIANT,VARIANT*)>(manager,0x1c)(manager,name,missing,missing,&out.v);
    SysFreeString(name);Check(hr);return out;
}
inline int Number(void* property,const wchar_t* key,int fallback) {
    Value value;Get(property,key,&value.v);
    if(Empty(value.v))return fallback;
    if(value.v.vt!=VT_I4)throw E_INVALIDARG;return value.v.lVal;
}
inline VARIANT Borrow(void* p){VARIANT v{};v.vt=VT_UNKNOWN;v.punkVal=static_cast<IUnknown*>(p);return v;}
struct State {
    int id=0;Object effect,layer;void* origin=nullptr;void* overlay=nullptr;
    unsigned color=0xffffffff;
    std::wstring sequence;bool failed=false;
};
inline void Reset(State& state) {
    DreamCanvas::Release(state.layer.p);state.layer.p=nullptr;
    DreamCanvas::Release(state.effect.p);state.effect.p=nullptr;
    state.id=0;state.origin=nullptr;state.overlay=nullptr;state.color=0xffffffff;state.sequence.clear();state.failed=false;
}
using EquipmentStates=std::array<State,12>;
inline std::map<void*,EquipmentStates>& States(){static auto* p=new std::map<void*,EquipmentStates>;return *p;}
inline const wchar_t* Category(int id) {
    switch(id/10000) {
    case 100:return L"Cap";case 102:return L"Accessory";case 105:return L"Longcoat";
    case 107:return L"Shoes";case 110:return L"Cape";default:return nullptr;
    }
}
inline Object EffectFor(int id,void* manager=nullptr) {
    auto category=Category(id);if(!category)return Object();
    wchar_t name[96];swprintf_s(name,L"Character/%s/%08d.img",category,id);
    auto image=Resource(name,manager);auto root=Query(image.v,PropertyIID());if(!root.p)return Object();
    auto infoValue=Resolve(root.p,L"info");auto info=Query(infoValue.v,PropertyIID());
    if(!info.p || Number(info.p,L"cash",0)!=1)return Object();
    if(id/10000==110) {
        // Old capes already converted to Character frames must not play twice.
        auto slot=Resolve(info.p,L"islot");
        if(slot.v.vt!=VT_BSTR || !slot.v.bstrVal || wcscmp(slot.v.bstrVal,L"Sr"))return Object();
        auto defaults=Resolve(root.p,L"default");auto directory=Query(defaults.v,PropertyIID());
        if(!directory.p || Query(defaults.v,CanvasIID()).p)return Object();
        auto placeholder=Resolve(directory.p,L"default");auto canvas=Query(placeholder.v,CanvasIID());
        if(!canvas.p || Int(canvas.p,0x40)!=1 || Int(canvas.p,0x48)!=1)return Object();
    }
    swprintf_s(name,L"Effect/ItemEff.img/%d/effect",id);
    auto resource=Resource(name,manager);auto effect=Query(resource.v,PropertyIID());
    if(!effect.p || Number(effect.p,L"action",0)!=1)return Object();
    auto frames=Resolve(effect.p,L"default");if(!Query(frames.v,PropertyIID()).p)return Object();
    return effect;
}
inline std::wstring Sequence(void* effect,const std::wstring& action) {
    if(!action.empty()) {
        auto value=Resolve(effect,action);auto property=Query(value.v,PropertyIID());
        if(property.p) {auto first=Resolve(effect,action+L"/0");if(Query(first.v,CanvasIID()).p)return action;}
    }
    return L"default";
}
inline bool IsRotationAction(const std::wstring& action,void* manager) {
    // CMS228 CActionMan::LoadActionData (1412F98BE..1412F992B) indexes
    // whole actions if ANY frame rotates or refers to prone. CUser then
    // removes actionExceptRotation effects for the entire action, including
    // its zero-angle frames (144B1AE5E..144B1B52F).
    static std::map<std::wstring,bool> cache;
    auto found=cache.find(action);if(found!=cache.end())return found->second;
    if(!manager)return false;
    bool excluded=action==L"prone";
    auto value=Resource(L"Character/00002000.img/"+action,manager);
    auto frames=Query(value.v,PropertyIID());if(!frames.p)return excluded;
    for(const auto& key:Names(frames.p)) {
        if(key.empty() || key.find_first_not_of(L"0123456789")!=std::wstring::npos)continue;
        auto frameValue=Resolve(frames.p,key);auto frame=Query(frameValue.v,PropertyIID());
        if(!frame.p)continue;
        auto pose=Resolve(frame.p,L"action");
        if(Number(frame.p,L"rotate",0)!=0 ||
           (pose.v.vt==VT_BSTR && pose.v.bstrVal && !wcscmp(pose.v.bstrVal,L"prone")))excluded=true;
    }
    cache.emplace(action,excluded);return excluded;
}
inline void* FrameOrigin(void* avatar,void* effect,const std::wstring& sequence) {
    auto value=Resolve(effect,sequence);auto frames=Query(value.v,PropertyIID());
    if(!avatar || !frames.p)throw E_POINTER;
    // Native CUser effect placement (941536..9416DF): pos=0 uses the
    // avatar body vector; pos=1 calls 932CBF for the head/brow vector.
    // CAvatar::Update moves that vector for each frame and mirrors its X.
    // Keep the actual vector, including chair/mount offsets, rather than
    // baking a standing-height correction into the source canvases.
    const int pos=Number(frames.p,L"pos",0);
    if(pos!=0 && pos!=1)throw E_INVALIDARG;
    return *reinterpret_cast<void**>(static_cast<BYTE*>(avatar)+(pos==1?0x10b4:0x10b8));
}
inline int LayerZ(void* effect,void* frames) {
    Value z;Get(frames,L"z",&z.v);if(Empty(z.v))Get(effect,L"z",&z.v);
    if(Empty(z.v))return -1;
    // The native face layer is the parent for eye accessory effects. This
    // keeps them above the expression rather than behind the whole avatar.
    if(z.v.vt==VT_BSTR && z.v.bstrVal && !wcscmp(z.v.bstrVal,L"accessoryEye"))return 1;
    if(z.v.vt!=VT_I4 || z.v.lVal < -100 || z.v.lVal > 100)throw E_INVALIDARG;
    return z.v.lVal;
}
inline Object BuildLayer(void* effect,const std::wstring& sequence,void* graphics,void* origin,void* overlay,int flip,unsigned color=0xffffffff) {
    auto value=Resolve(effect,sequence);auto frames=Query(value.v,PropertyIID());
    if(!frames.p || !graphics || !origin || !overlay)throw E_POINTER;
    int z=LayerZ(effect,frames.p);
    Object layer;VARIANT missing{};missing.vt=VT_ERROR;missing.scode=DISP_E_PARAMNOTFOUND;
    Check(Method<HRESULT(__stdcall*)(void*,int,int,unsigned,unsigned,int,VARIANT,VARIANT,void**)>(graphics,0x64)
        (graphics,0,0,0,0,z,Integer(0),missing,&layer.p));
    if(!layer.p)throw E_POINTER;
    Check(Method<HRESULT(__stdcall*)(void*,VARIANT)>(layer.p,0x64)(layer.p,Borrow(origin)));
    Check(Method<HRESULT(__stdcall*)(void*,VARIANT)>(layer.p,0xfc)(layer.p,Borrow(overlay)));
    Check(Method<HRESULT(__stdcall*)(void*,int)>(layer.p,0xd8)(layer.p,flip));
    // In v83 put_color writes the SAME alpha vector that Animate updates.
    // Copying the avatar color every tick fights the next frame's a0/a1:
    // GM hide (128) alternates with the source frame's 255. Give the native
    // animation already-scaled endpoints and leave its alpha clock alone.
    Check(Method<HRESULT(__stdcall*)(void*,unsigned)>(layer.p,0xe0)(layer.p,color));
    const unsigned opacity=color>>24;
    unsigned count=0;
    for(;count<256;++count) {
        auto frame=Resolve(effect,sequence+L"/"+std::to_wstring(count));if(Empty(frame.v))break;
        auto canvas=Query(frame.v,CanvasIID());if(!canvas.p)throw E_INVALIDARG;
        auto metadata=CanvasProperty(canvas.p);
        int delay=Number(metadata.p,L"delay",100);
        if(delay<=0 || delay>60000)throw E_INVALIDARG;
        Value inserted;
        Check(Method<HRESULT(__stdcall*)(void*,void*,VARIANT,VARIANT,VARIANT,VARIANT,VARIANT,VARIANT*)>(layer.p,0x104)
            (layer.p,canvas.p,Integer(delay),Integer((Number(metadata.p,L"a0",255)*opacity+127)/255),Integer((Number(metadata.p,L"a1",255)*opacity+127)/255),
             Integer(Number(metadata.p,L"z0",100)),Integer(Number(metadata.p,L"z1",100)),&inserted.v));
    }
    if(!count || count==256)throw E_INVALIDARG;
    Check(Method<HRESULT(__stdcall*)(void*,int,VARIANT,VARIANT)>(layer.p,0x110)(layer.p,0x20,missing,missing));
    return layer;
}
inline void SyncSequence(State& state,void* graphics,void* avatar,void* overlay,const std::wstring& sequence,int flip,const std::wstring& anchorSequence=L"") {
    auto origin=FrameOrigin(avatar,state.effect.p,anchorSequence.empty()?sequence:anchorSequence);
    auto framesValue=Resolve(state.effect.p,sequence);auto frames=Query(framesValue.v,PropertyIID());
    Value depth;Get(frames.p,L"z",&depth.v);if(Empty(depth.v))Get(state.effect.p,L"z",&depth.v);
    void* parent=overlay;
    if(depth.v.vt==VT_BSTR && depth.v.bstrVal && !wcscmp(depth.v.bstrVal,L"accessoryEye"))
        parent=*reinterpret_cast<void**>(static_cast<BYTE*>(avatar)+0x10c0);
    // Avatar vectors/layers can be absent while a look is being rebuilt.
    // Retry next update instead of permanently disabling this cape.
    if(!origin || !overlay || !parent) {DreamCanvas::Release(state.layer.p);state.layer.p=nullptr;return;}
    const unsigned color=static_cast<unsigned>(Int(overlay,0xdc));
    if(!state.layer.p || state.sequence!=sequence || state.origin!=origin || state.overlay!=parent || state.color!=color) {
        auto replacement=BuildLayer(state.effect.p,sequence,graphics,origin,parent,flip,color);
        DreamCanvas::Release(state.layer.p);state.layer.p=replacement.detach();
        state.sequence=sequence;state.origin=origin;state.overlay=parent;state.color=color;
    }
    // Changing facing does not restart the animation clock.
    Check(Method<HRESULT(__stdcall*)(void*,int)>(state.layer.p,0xd8)(state.layer.p,flip));
    Check(Method<HRESULT(__stdcall*)(void*,int)>(state.layer.p,0x11c)(state.layer.p,Int(overlay,0x118)));
}
inline void Sync(State& state,void* graphics,void* avatar,void* overlay,const std::wstring& action,int flip,bool rotationAction=false) {
    if(rotationAction && Number(state.effect.p,L"actionExceptRotation",0)!=0) {
        DreamCanvas::Release(state.layer.p);state.layer.p=nullptr;return;
    }
    SyncSequence(state,graphics,avatar,overlay,Sequence(state.effect.p,action),flip);
}
inline std::wstring ActionName(int action) {
    if(action<0 || action>=AvatarActionIds::Count)return L"default";
    // Read the existing instruction operand; Evan's extended table relocates
    // this pointer, so the old global table address must not be hard-coded.
    auto table=*reinterpret_cast<BYTE**>(0x00452842);
    auto data=*reinterpret_cast<wchar_t***>(table+action*24);
    if(!data || !*data || wcslen(*data)>64)return L"default";
    return *data;
}
inline void TickEquipment(void* avatar,void* manager,void* graphics,const std::wstring& action,int flip) {
    auto& states=States();auto found=states.find(avatar);
    if(found==states.end() && states.size()>=512)return;
    auto& equipment=states[avatar];
    auto overlay=*reinterpret_cast<void**>(static_cast<BYTE*>(avatar)+0x10c8);
    bool rotationAction=false;
    try{rotationAction=IsRotationAction(action,manager);}catch(...){}
    // CAvatar begins with a vtable then AvatarLook. Its visible equip array
    // starts at +1D; slot 9 is +41 (cape), 7 is +39 (shoes). Read every
    // supported visible slot independently so multiple effects can coexist.
    for(int slot:{1,3,5,7,9}) {
        int id=0;std::memcpy(&id,static_cast<BYTE*>(avatar)+0x1d+slot*4,4);
        auto& state=equipment[slot];
        if(state.id!=id) {
            Reset(state);state.id=id;
            try{auto effect=EffectFor(id,manager);state.effect.p=effect.detach();}catch(...){state.failed=true;}
        }
        if(!state.effect.p || state.failed)continue;
        try {
            Sync(state,graphics,avatar,overlay,action,flip,rotationAction);
            if(state.layer.p && HiddenByChair() && HiddenByChair()(avatar,id))
                Check(Method<HRESULT(__stdcall*)(void*,int)>(state.layer.p,0x11c)(state.layer.p,0));
        } catch(...) {
            DreamCanvas::Release(state.layer.p);state.layer.p=nullptr;state.failed=true;
            static LONG reports=0;
            if(InterlockedIncrement(&reports)<=24)CrashReporter::RecordEvent("equipment.item-effect","item=%d layer_failed",id);
        }
    }
}
inline void Tick(void* avatar) {
    int flip=0;int action=actionFor(avatar,&flip);
    auto manager=*reinterpret_cast<void**>(0x00BF14E8),graphics=*reinterpret_cast<void**>(0x00BF14EC);
    auto name=ActionName(action);
    TickEquipment(avatar,manager,graphics,name,flip);
    if(UpdateSetEffects())UpdateSetEffects()(avatar,manager,graphics,name,flip);
}
static void __fastcall Update(void* avatar,void*) {update(avatar);Tick(avatar);}
static void __fastcall Destroy(void* avatar,void*) {
    States().erase(avatar);
    if(OnSetEffectsDestroyed())OnSetEffectsDestroyed()(avatar);
    if(OnAvatarDestroyed())OnAvatarDestroyed()(avatar);
    destroy(avatar);
}
static bool Install() {
    const BYTE u[]={0xb8,0xf6,0xc1,0xa7,0,0xe8},d[]={0xb8,0xaa,0xbe,0xa7,0,0xe8};
    if(std::memcmp(reinterpret_cast<void*>(update),u,sizeof(u)) || std::memcmp(reinterpret_cast<void*>(destroy),d,sizeof(d)))return false;
    if(!Memory::SetHook(true,reinterpret_cast<void**>(&destroy),Destroy))return false;
    if(Memory::SetHook(true,reinterpret_cast<void**>(&update),Update))return true;
    Memory::SetHook(false,reinterpret_cast<void**>(&destroy),Destroy);return false;
}
}
