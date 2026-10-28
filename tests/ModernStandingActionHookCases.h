#pragma once

// Only isolated mapped call sites execute here, never the game entry point.
static void CheckStandingActionInstallation() {
    const DWORD sites[]={0x004522d4,0x00453b0a};
    BYTE original[2][5],getter[16];
    for(int i=0;i<2;++i)std::memcpy(original[i],reinterpret_cast<void*>(sites[i]),5);
    std::memcpy(getter,reinterpret_cast<void*>(0x00451e4c),sizeof(getter));
    *reinterpret_cast<BYTE*>(sites[1])=0x90;
    Require(!ModernStandingActions::Install(),"standing hook rejects unexpected executable signature");
    Require(!std::memcmp(original[0],reinterpret_cast<void*>(sites[0]),5),"rejected signature leaves the other caller unchanged");
    std::memcpy(reinterpret_cast<void*>(sites[1]),original[1],5);
    DWORD ignored=0;
    for(DWORD site:sites)Require(VirtualProtect(reinterpret_cast<void*>(site),5,PAGE_EXECUTE_READ,&ignored)!=FALSE,"standing caller is read-only before install");
    for(int fail:{1,2}) {
        protectionCalls=0;deniedProtectionCall=fail;
        Require(!ModernStandingActions::Install(),"standing protection failure rejects installation");
        for(int i=0;i<2;++i) {
            Require(!std::memcmp(original[i],reinterpret_cast<void*>(sites[i]),5),"standing protection failure preserves both calls");
            MEMORY_BASIC_INFORMATION page{};
            Require(VirtualQuery(reinterpret_cast<void*>(sites[i]),&page,sizeof(page)) && page.Protect==PAGE_EXECUTE_READ,"standing protection rollback restores each page");
        }
    }
    deniedProtectionCall=0;
    Require(ModernStandingActions::Install(),"both original rendering callers accept standing bridge");
    for(DWORD site:sites) {
        Require(site+5+*reinterpret_cast<DWORD*>(site+1)==reinterpret_cast<DWORD>(ModernStandingActions::RenderAction),"standing rendering calls target the tested bridge");
        MEMORY_BASIC_INFORMATION page{};
        Require(VirtualQuery(reinterpret_cast<void*>(site),&page,sizeof(page)) && page.Protect==PAGE_EXECUTE_READ,"standing installation restores caller page protection");
    }
    Require(!std::memcmp(getter,reinterpret_cast<void*>(0x00451e4c),sizeof(getter)),"native action getter remains available to gameplay callers");
    Require(!ModernStandingActions::Install(),"duplicate standing installation is rejected");
}

static int standingNativeAction=2;
static void* standingAvatarSeen=nullptr;
static int __fastcall StandingNativeGetter(void* avatar,void*,int* flip) {
    standingAvatarSeen=avatar;*flip=1;return standingNativeAction;
}
static void CheckStandingActionRouting(void* manager) {
    auto saved=ModernCapeEffects::actionFor;
    ModernCapeEffects::actionFor=reinterpret_cast<ModernCapeEffects::ActionCall>(StandingNativeGetter);
    BYTE avatar[0x1100]{};
    auto bound=reinterpret_cast<int*>(0x00453b2d);
    const int savedBound=*bound;DWORD protection=0,ignored=0;
    Require(VirtualProtect(bound,4,PAGE_EXECUTE_READWRITE,&protection)!=FALSE,"standing native bounds test setup");
    Require(*reinterpret_cast<void**>(0x00BF14E8)==manager,"standing bridge uses the mounted resource manager");
    for(DWORD site:{0x004522d4,0x00453b0a}) {
        auto call=reinterpret_cast<ModernCapeEffects::ActionCall>(site+5+*reinterpret_cast<DWORD*>(site+1));
        for(int item:{1053910,1053909}) {
            std::memcpy(avatar+0x31,&item,4);
            for(int action:{2,3,4,12,162}) {
                *bound=AvatarActionIds::Count;standingNativeAction=action;int flip=0;
                const int expected=action==2?167:action==3?168:action;
                Require(call(avatar,&flip)==expected && flip==1 && standingAvatarSeen==avatar,"standing bridge preserves thiscall stack/this/flip and routes original resources");
            }
            *bound=AvatarActionIds::NativeCount;standingNativeAction=2;int flip=0;
            Require(call(avatar,&flip)==2,"absent extended action banks prevent returning unsupported indices");
        }
    }
    *bound=savedBound;
    Require(VirtualProtect(bound,4,protection,&ignored)!=FALSE,"standing bounds page restored");
    ModernCapeEffects::actionFor=saved;
    std::puts("PASS: original standing call signatures, transactional protection failure, native gameplay getter preserved, thiscall ABI, both outfits/stances and missing-table fallback");
}
