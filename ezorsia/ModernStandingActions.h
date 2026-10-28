#pragma once
#include "ModernCapeEffects.h"

namespace ModernStandingActions {
using namespace MixedDyeResources;
inline int Replacement(int id,void* manager) {
    static std::map<int,int> cache;
    if(!ModernCapeEffects::Category(id) || !manager)return 0;
    auto found=cache.find(id);if(found!=cache.end())return found->second;
    auto effect=ModernCapeEffects::EffectFor(id,manager);
    const int replacement=effect.p?ModernCapeEffects::Number(effect.p,L"replacedStandAction",0):0;
    cache.emplace(id,replacement);return replacement;
}
inline int Select(int action,void* avatar,void* manager) {
    if(!avatar || !manager || (action!=2 && action!=3))return action;
    const auto bytes=static_cast<BYTE*>(avatar);
    // Native mounted/morphed avatars use separate animation banks. Only the
    // ordinary standing action is replaced; gameplay movement IDs stay native.
    if(*reinterpret_cast<int*>(bytes+0x4a0) || *reinterpret_cast<int*>(bytes+0x4b4))return action;
    int replacement=0;
    for(int slot:{1,3,5,7,9}) {
        int id=0;std::memcpy(&id,bytes+0x1d+slot*4,4);
        if(ModernCapeEffects::HiddenByChair() && ModernCapeEffects::HiddenByChair()(avatar,id))continue;
        replacement=(std::max)(replacement,Replacement(id,manager));
    }
    // CMS228 141B3E4B1 selects stand1_floating/stand2_floating for value 1.
    // Values 2/3 refer to different original actions and must not be guessed.
    return replacement==1 ? AvatarActionIds::FloatingStand1+action-2 : action;
}
static int __fastcall RenderAction(void* avatar,void*,int* flip) {
    const int action=ModernCapeEffects::actionFor(avatar,flip);
    // Evan's shared table installer runs separately. Never return an appended
    // index if its bounds/cache installation was rejected for this executable.
    if(*reinterpret_cast<int*>(0x00453b2d)!=AvatarActionIds::Count)return action;
    try{return Select(action,avatar,*reinterpret_cast<void**>(0x00BF14E8));}
    catch(...){return action;}
}
inline bool Install() {
    // Patch just the rendering callers: CAvatar::Update/PrepareActionLayer.
    // The same getter is used by movement and attacks; those callers continue
    // to observe native stand1/stand2 and never receive the appended IDs.
    const DWORD sites[]={0x004522d4,0x00453b0a};
    for(DWORD site:sites) {
        auto code=reinterpret_cast<const BYTE*>(site);
        if(code[0]!=0xe8 || site+5+*reinterpret_cast<const DWORD*>(code+1)!=0x00451e4c)return false;
    }
    DWORD protections[2]{};int acquired=0;
    for(DWORD site:sites) {
        if(!VirtualProtect(reinterpret_cast<void*>(site),5,PAGE_EXECUTE_READWRITE,&protections[acquired])) {
            while(acquired){--acquired;DWORD ignored;VirtualProtect(reinterpret_cast<void*>(sites[acquired]),5,protections[acquired],&ignored);}
            return false;
        }
        ++acquired;
    }
    for(DWORD site:sites) {
        const DWORD relative=reinterpret_cast<DWORD>(RenderAction)-(site+5);
        std::memcpy(reinterpret_cast<void*>(site+1),&relative,4);
        FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(site),5);
    }
    while(acquired){--acquired;DWORD ignored;VirtualProtect(reinterpret_cast<void*>(sites[acquired]),5,protections[acquired],&ignored);}
    return true;
}
}
