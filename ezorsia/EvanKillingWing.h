#pragma once
#include <map>
#include <vector>
#include <cstring>

// Append real actions instead of replacing a live v83 action. CAvatar's two
// fixed-size animation banks retain their ABI; only the new action's two cache
// cells live outside the object, with the same native reset/destruction rules.
namespace EvanKillingWing {
#ifdef EVAN_RUNTIME_TEST
constexpr DWORD Address(DWORD value) { return value + 0x10000000; }
#else
constexpr DWORD Address(DWORD value) { return value; }
#endif
constexpr int AvatarCount = 163, DragonCount = 22, Action = 162;
struct ActionData { void* name; int delay, relative, duration, attackDelay; void* frames; };
static_assert(sizeof(ActionData)==24, "native action ABI");
static ActionData avatar[AvatarCount]{};
static void* dragon[DragonCount]{};
struct Cache { void* normal=nullptr; void* mounted=nullptr; };
static std::map<void*,Cache> extraCaches;
static Cache* __cdecl CacheFor(void* bank) { return &extraCaches[bank]; }
static void __cdecl ClearCache(void* bank) {
    auto found=extraCaches.find(bank);
    if(found==extraCaches.end()) return;
    reinterpret_cast<void(__thiscall*)(void*)>(Address(0x00457235))(&found->second.normal);
    reinterpret_cast<void(__thiscall*)(void*)>(Address(0x00457240))(&found->second.mounted);
    extraCaches.erase(found);
}
static void __cdecl InitAvatar() {
    // The native ACTIONDATA constructor uses (delay=0, relative=1) for skill actions.
    reinterpret_cast<void*(__thiscall*)(void*,const wchar_t*)>(Address(0x00403382))(&avatar[Action].name,L"killingWing");
    avatar[Action].relative=1;
}
static void __cdecl InitDragon() {
    reinterpret_cast<void*(__thiscall*)(void*,const wchar_t*)>(Address(0x00403382))(&dragon[21],L"killingWing");
}
static const DWORD avatarInitReturn=Address(0x004A60DA), dragonInitReturn=Address(0x004A9CA9);
static const DWORD prepareReturn=Address(0x0045456F), updateReturn=Address(0x004527AA);
static const DWORD clearReturn=Address(0x00453A3F), destroyReturn=Address(0x00450098);
static const DWORD destroyHandler=Address(0x00A7BD97);
__declspec(naked) static void AvatarInit() {
    __asm {
        pushfd
        pushad
        call InitAvatar
        popad
        popfd
        mov ecx,[ebp-0ch]
        pop edi
        pop esi
        jmp dword ptr [avatarInitReturn]
    }
}
__declspec(naked) static void DragonInit() {
    __asm {
        pushfd
        pushad
        call InitDragon
        popad
        popfd
        mov ecx,[ebp-0ch]
        mov fs:[0],ecx
        jmp dword ptr [dragonInitReturn]
    }
}
__declspec(naked) static void PrepareCache() {
    __asm {
        pushfd
        cmp ebx,162
        jne original
        push edx
        push eax
        call CacheFor
        add esp,4
        pop edx
        mov ecx,eax
        add eax,4
        popfd
        jmp dword ptr [prepareReturn]
    original:
        popfd
        lea ecx,[eax+ebx*4+10h]
        lea eax,[eax+ebx*4+298h]
        jmp dword ptr [prepareReturn]
    }
}
__declspec(naked) static void UpdateCache() {
    __asm {
        pushfd
        cmp edi,162
        jne original
        push ecx
        push edx
        push esi
        call CacheFor
        add esp,4
        pop edx
        pop ecx
        mov [ebp-24h],eax
        add eax,4
        popfd
        jmp dword ptr [updateReturn]
    original:
        popfd
        lea eax,[esi+edi*4+10h]
        mov [ebp-24h],eax
        lea eax,[esi+edi*4+298h]
        jmp dword ptr [updateReturn]
    }
}
__declspec(naked) static void ResetCache() {
    __asm {
        lea esi,[eax+ecx+4f0h]
        pushfd
        pushad
        push esi
        call ClearCache
        add esp,4
        popad
        popfd
        jmp dword ptr [clearReturn]
    }
}
__declspec(naked) static void DestroyCache() {
    __asm {
        pushfd
        pushad
        push ecx
        call ClearCache
        add esp,4
        popad
        popfd
        mov eax,dword ptr [destroyHandler]
        jmp dword ptr [destroyReturn]
    }
}
// GMS084 0099247B admits Killer Wings into the original lt/rb selection branch.
static const DWORD rangeReturn=Address(0x00955E2C), rectangle=Address(0x00956372);
__declspec(naked) static void RangeGate() {
    __asm {
        je selected
        cmp eax,22151002
        je selected
        jmp dword ptr [rangeReturn]
    selected:
        jmp dword ptr [rectangle]
    }
}
struct PointerPatch { DWORD operand, before, offset; bool dragon; };
static const PointerPatch pointers[]={
#include "EvanActionTableRefs.inc"
};
struct Patch { DWORD site; std::vector<unsigned char> before,after; };
static std::vector<Patch> BuildPatches() {
    std::vector<Patch> result;
    for(const auto& p:pointers) {
        DWORD before=p.before;
        DWORD after=reinterpret_cast<DWORD>(p.dragon?static_cast<void*>(dragon):static_cast<void*>(avatar))+p.offset;
        const auto b=reinterpret_cast<unsigned char*>(&before),a=reinterpret_cast<unsigned char*>(&after);
        result.push_back({Address(p.operand),{b,b+4},{a,a+4}});
    }
    auto bytes=[&](DWORD site,std::initializer_list<unsigned char> before,std::initializer_list<unsigned char> after) {
        result.push_back({Address(site),before,after});
    };
    auto jump=[&](DWORD site,std::initializer_list<unsigned char> before,void* destination) {
        std::vector<unsigned char> after(before.size(),0x90);after[0]=0xe9;
        DWORD distance=reinterpret_cast<DWORD>(destination)-(Address(site)+5);
        std::memcpy(after.data()+1,&distance,4);
        result.push_back({Address(site),before,after});
    };
    // CActionMan must load the appended action's metadata and allocate its
    // per-resource arrays too; extending name lookup alone leaves null frames.
    bytes(0x004073A2,{0x81,0xfe,0xa2,0,0,0},{0x81,0xfe,0xa3,0,0,0});
    bytes(0x0040ACDA,{0xbe,0xa2,0,0,0},{0xbe,0xa3,0,0,0});
    bytes(0x0040ACFE,{0x68,0x20,0x0a,0,0},{0x68,0x30,0x0a,0,0});
    bytes(0x0040B2A2,{0x81,0x7d,0xd0,0x60,0x1e,0,0},{0x81,0x7d,0xd0,0x90,0x1e,0,0});
    bytes(0x00453B2B,{0x81,0xfb,0xa2,0,0,0},{0x81,0xfb,0xa3,0,0,0});
    bytes(0x004522F4,{0x81,0x7d,0xec,0xa2,0,0,0},{0x81,0x7d,0xec,0xa3,0,0,0});
    bytes(0x00451010,{0x68,0xa2,0,0,0},{0x68,0xa3,0,0,0}); // dynamic per-action flags
    bytes(0x004A60F5,{0x68,0xa2,0,0,0},{0x68,0xa3,0,0,0}); // action metadata destructor count
    bytes(0x004A9CBC,{0x6a,0x15},{0x6a,0x16}); // dragon name destructor count
    bytes(0x004FEC48,{0x6a,0x15},{0x6a,0x16}); // dynamic dragon action cache
    for (DWORD site : {0x0092EEDC,0x0096AF58,0x0096CACE,0x0096D4E1})
        bytes(site,{0x83,0xf8,0x15},{0x83,0xf8,0x16});
    jump(0x004A60D5,{0x8b,0x4d,0xf4,0x5f,0x5e},AvatarInit);
    jump(0x004A9C9F,{0x8b,0x4d,0xf4,0x64,0x89,0x0d,0,0,0,0},DragonInit);
    jump(0x00454564,{0x8d,0x4c,0x98,0x10,0x8d,0x84,0x98,0x98,0x02,0,0},PrepareCache);
    jump(0x0045279C,{0x8d,0x44,0xbe,0x10,0x89,0x45,0xdc,0x8d,0x84,0xbe,0x98,0x02,0,0},UpdateCache);
    jump(0x00453A38,{0x8d,0xb4,0x08,0xf0,0x04,0,0},ResetCache);
    jump(0x00450093,{0xb8,0x97,0xbd,0xa7,0},DestroyCache);
    jump(0x00955E26,{0x0f,0x84,0x46,0x05,0,0},RangeGate);
    return result;
}
static bool Validate() {
    for(const auto& p:BuildPatches())
        if(std::memcmp(reinterpret_cast<void*>(p.site),p.before.data(),p.before.size())) {
#ifdef EVAN_RUNTIME_TEST
            printf("Killer Wings signature mismatch at %08lx\n",p.site);
#endif
            return false;
        }
    return true;
}
static bool Install() {
    auto patches=BuildPatches();
    if(!Validate()) return false;
    std::vector<DWORD> protections(patches.size());
    size_t acquired=0;
    for(const auto& p:patches) {
        if(!VirtualProtect(reinterpret_cast<void*>(p.site),p.after.size(),PAGE_EXECUTE_READWRITE,&protections[acquired])) {
            for(size_t i=acquired;i>0;--i) {DWORD ignored;VirtualProtect(reinterpret_cast<void*>(patches[i-1].site),patches[i-1].after.size(),protections[i-1],&ignored);}
            return false;
        }
        ++acquired;
    }
    for(const auto& p:patches) {std::memcpy(reinterpret_cast<void*>(p.site),p.after.data(),p.after.size());FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(p.site),p.after.size());}
    for(size_t i=acquired;i>0;--i) {DWORD ignored;VirtualProtect(reinterpret_cast<void*>(patches[i-1].site),patches[i-1].after.size(),protections[i-1],&ignored);}
    return true;
}
}
