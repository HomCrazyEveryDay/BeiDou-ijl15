#pragma once
#include "ModernCapeEffects.h"
#include <random>
#include <mutex>

// v83's CUser::SetSetItemEffect (9393FC) only passes effect/0.. to its
// layer loader. Modern cash sets instead hold action/randEffect animations.
// Keep source properties intact and give each avatar its own selection/layer.
namespace ModernSetEffects {
using namespace MixedDyeResources;
using ModernCapeEffects::Number;
struct Requirement {int slot;std::vector<int> items;};
struct Definition {
    int id=0;Object effect;std::vector<Requirement> required;
    std::map<std::wstring,std::vector<std::wstring>> actions;
};
inline int Numeric(const std::wstring& name) {
    if(name.empty() || name.size()>7 || name.find_first_not_of(L"0123456789")!=std::wstring::npos)return -1;
    return _wtoi(name.c_str());
}
inline bool ReadDefinition(void* root,int id,Definition& result) {
    if(Number(root,L"cash",0)!=1)return false;
    auto effectValue=Resolve(root,L"effect");auto effect=Query(effectValue.v,PropertyIID());
    if(!effect.p || Number(effect.p,L"action",0)!=1)return false;
    auto infoValue=Resolve(root,L"info");auto info=Query(infoValue.v,PropertyIID());if(!info.p)return false;
    for(const auto& key:Names(info.p)) {
        const int slot=Numeric(key);if(slot<1 || slot>11)return false;
        auto value=Resolve(info.p,key);auto options=Query(value.v,PropertyIID());if(!options.p)return false;
        Requirement requirement;requirement.slot=slot;
        for(const auto& option:Names(options.p)) {
            const int item=Number(options.p,option.c_str(),0);
            if(item<1000000 || item>=1800000)return false;
            requirement.items.push_back(item);
        }
        if(requirement.items.empty())return false;
        result.required.push_back(std::move(requirement));
    }
    if(result.required.empty())return false;
    for(const auto& action:Names(effect.p)) {
        auto value=Resolve(effect.p,action);auto property=Query(value.v,PropertyIID());if(!property.p)continue;
        if(Number(property.p,L"randEff",0)!=1)continue;
        auto variantsValue=Resolve(effect.p,action+L"/randEffect");auto variants=Query(variantsValue.v,PropertyIID());
        if(!variants.p)return false;
        std::vector<std::wstring> sequences;
        for(const auto& variant:Names(variants.p)) {
            if(Numeric(variant)<0)return false;
            auto path=action+L"/randEffect/"+variant;
            auto frame=Resolve(effect.p,path+L"/0");if(!Query(frame.v,CanvasIID()).p)return false;
            sequences.push_back(path);
        }
        if(sequences.empty() || sequences.size()>64)return false;
        result.actions.emplace(action,std::move(sequences));
    }
    if(result.actions.empty())return false;
    result.id=id;result.effect.p=effect.detach();return true;
}
struct Catalog {
    std::recursive_mutex mutex;Object manager,legacyRoot;bool loaded=false;
    std::vector<Definition> definitions;
};
inline Catalog& Resources(){static auto* value=new Catalog;return *value;}
inline void Prepare(Catalog& catalog,void* manager) {
    if(catalog.manager.p!=manager) {
        catalog.definitions.clear();catalog.loaded=false;
        DreamCanvas::Release(catalog.legacyRoot.p);catalog.legacyRoot.p=nullptr;
        DreamCanvas::Release(catalog.manager.p);catalog.manager.p=manager;
        if(manager)static_cast<IUnknown*>(manager)->AddRef();
    }
}
inline void LoadRoot(Catalog& catalog,void* root) {
    if(catalog.loaded || !root)return;
    std::vector<Definition> definitions;
    for(const auto& name:Names(root)) {
        const int id=Numeric(name);if(id<0)continue;
        auto entry=Resolve(root,name);auto property=Query(entry.v,PropertyIID());if(!property.p)continue;
        Definition definition;
        try {if(ReadDefinition(property.p,id,definition))definitions.push_back(std::move(definition));}
        catch(...) { /* One unsupported set must not disable the other sets. */ }
    }
    catalog.definitions=std::move(definitions);catalog.loaded=true;
}
inline void Load(Catalog& catalog,void* manager) {
    Prepare(catalog,manager);if(catalog.loaded || !manager)return;
    auto value=ModernCapeEffects::Resource(L"Effect/SetEff.img",manager);auto root=Query(value.v,PropertyIID());
    // The installed resource hook discovers the complete source before it
    // returns the native-only view. Standalone readers use the original root.
    LoadRoot(catalog,root.p);
}
using Look=std::array<int,12>;
inline Look VisibleLook(void* avatar) {
    Look look{};for(int slot=1;slot<12;++slot)std::memcpy(&look[slot],static_cast<BYTE*>(avatar)+0x1d+slot*4,4);
    return look;
}
inline bool Matches(const Definition& definition,const Look& look) {
    for(const auto& r:definition.required)
        if(std::find(r.items.begin(),r.items.end(),look[r.slot])==r.items.end())return false;
    return true;
}
struct Animation {ModernCapeEffects::State render;std::wstring action,sequence;};
struct AvatarState {
    Look look{};void* manager=nullptr;bool matched=false;std::map<int,Animation> animations;
    std::mt19937 random;
    AvatarState() {
        static LONG serial=0;LARGE_INTEGER ticks{};QueryPerformanceCounter(&ticks);
        random.seed(static_cast<unsigned>(ticks.LowPart)^static_cast<unsigned>(InterlockedIncrement(&serial))*0x9e3779b9u);
    }
};
inline std::map<void*,AvatarState>& States(){static auto* states=new std::map<void*,AvatarState>;return *states;}
inline void ClearAnimation(Animation& animation) {
    DreamCanvas::Release(animation.render.layer.p);animation.render.layer.p=nullptr;
    animation.action.clear();animation.sequence.clear();animation.render.sequence.clear();
}
inline void Update(AvatarState& state,const std::vector<Definition>& definitions,void* avatar,void* manager,
                   void* graphics,const std::wstring& action,int flip) {
    auto look=VisibleLook(avatar);
    if(!state.matched || state.look!=look || state.manager!=manager) {
        state.animations.clear();state.look=look;state.manager=manager;state.matched=true;
        for(const auto& definition:definitions)if(Matches(definition,look)) {
            auto& render=state.animations[definition.id].render;render.id=definition.id;
            render.effect.p=definition.effect.p;static_cast<IUnknown*>(render.effect.p)->AddRef();
        }
    }
    auto overlay=*reinterpret_cast<void**>(static_cast<BYTE*>(avatar)+0x10c8);
    for(const auto& definition:definitions) {
        auto found=state.animations.find(definition.id);if(found==state.animations.end())continue;
        auto& animation=found->second;auto& render=animation.render;if(render.failed)continue;
        auto choice=definition.actions.find(action);
        if(choice==definition.actions.end())choice=definition.actions.find(L"default");
        // No walking/jumping sequence in these source sets: leave no stale
        // standing companion behind. A new eligible action selects once.
        if(choice==definition.actions.end()) {ClearAnimation(animation);continue;}
        try {
            if(animation.action!=choice->first || animation.sequence.empty()) {
                animation.action=choice->first;
                std::uniform_int_distribution<size_t> pick(0,choice->second.size()-1);
                animation.sequence=choice->second[pick(state.random)];
            }
            ModernCapeEffects::SyncSequence(render,graphics,avatar,overlay,animation.sequence,flip,animation.action);
            if(render.layer.p && ModernCapeEffects::HiddenByChair()) {
                for(const auto& requirement:definition.required)
                    if(ModernCapeEffects::HiddenByChair()(avatar,look[requirement.slot])) {
                        Check(Method<HRESULT(__stdcall*)(void*,int)>(render.layer.p,0x11c)(render.layer.p,0));break;
                    }
            }
        } catch(...) {
            ClearAnimation(animation);render.failed=true;
            static LONG reports=0;if(InterlockedIncrement(&reports)<=12)
                CrashReporter::RecordEvent("equipment.set-effect","set=%d layer_failed",definition.id);
        }
    }
}
inline void Tick(void* avatar,void* manager,void* graphics,const std::wstring& action,int flip) {
    if(!avatar || !manager || !graphics)return;
    try {
        auto& catalog=Resources();std::lock_guard<std::recursive_mutex> lock(catalog.mutex);Load(catalog,manager);
        if(!catalog.loaded)return;
        auto& states=States();if(states.find(avatar)==states.end() && states.size()>=512)return;
        Update(states[avatar],catalog.definitions,avatar,manager,graphics,action,flip);
    } catch(...) {
        States().erase(avatar);static LONG reports=0;
        if(InterlockedIncrement(&reports)<=8)CrashReporter::RecordEvent("equipment.set-effect","catalog_failed");
    }
}
inline bool& Enabled(){static bool enabled=false;return enabled;}
inline bool RootPath(BSTR path) {
    if(!path || SysStringLen(path)>128)return false;
    const wchar_t* start=path;if(*start==L'/')++start;if(wcsncmp(start,L"Data/",5)==0)start+=5;
    return !wcscmp(start,L"Effect/SetEff.img");
}
inline void AdaptNativeRoot(Factory factory,void* manager,BSTR path,VARIANT* value) {
    if(!Enabled() || !value || !RootPath(path))return;
    try {
        auto root=Query(*value,PropertyIID());if(!root.p)return;
        auto& catalog=Resources();std::lock_guard<std::recursive_mutex> lock(catalog.mutex);
        Prepare(catalog,manager);LoadRoot(catalog,root.p);if(catalog.definitions.empty())return;
        if(!catalog.legacyRoot.p) {
            auto legacy=New(factory,L"Property",PropertyIID());std::set<int> handled;
            for(const auto& definition:catalog.definitions)handled.insert(definition.id);
            for(const auto& key:Names(root.p))if(!handled.count(Numeric(key))) {
                Value original;Get(root.p,key,&original.v);Put(legacy.p,key,original.v);
            }
            catalog.legacyRoot.p=legacy.detach();
        }
        // 5D925D uses the legacy enumeration index as its set ID. Keep its
        // original entries/order, excluding sets owned by the modern pass;
        // otherwise appended IDs 152/153 would be misread as 101/102.
        Check(ResourceVariant::Clear(value));*value=ModernCapeEffects::Borrow(catalog.legacyRoot.p);
        value->punkVal->AddRef();
    } catch(...) { /* Preserve native behavior if catalog discovery fails. */ }
}
inline void Install(bool avatarHookInstalled) {
    Enabled()=avatarHookInstalled;
    if(!avatarHookInstalled) {
        ModernCapeEffects::UpdateSetEffects()=nullptr;
        ModernCapeEffects::OnSetEffectsDestroyed()=nullptr;return;
    }
    ModernCapeEffects::UpdateSetEffects()=Tick;
    ModernCapeEffects::OnSetEffectsDestroyed()=[](void* avatar){States().erase(avatar);};
}
}
