#include "stdafx.h"
#include "VisitorsMorph.h"
#include "ClientLog.h"
#include <cstdio>
#include <cstring>

namespace {
#ifdef VISITORS_MORPH_TEST
constexpr DWORD Native(DWORD address) { return address + 0x10000000; }
#else
constexpr DWORD Native(DWORD address) { return address; }
#endif

using GetJob = int(__thiscall*)(void*);
using FindSkill = void*(__thiscall*)(void*, int);
using CastSkill = int(__thiscall*)(void*, int, int, void*);
using SelectAction = int(__cdecl*)(int, int, void*, int, unsigned);
using ReadInt = int(__cdecl*)(const void*, unsigned);
const auto findSkill = reinterpret_cast<FindSkill>(Native(0x0075C755));
const auto castSkill = reinterpret_cast<CastSkill>(Native(0x00966F7A));
const auto selectAction = reinterpret_cast<SelectAction>(Native(0x00766031));
const auto readInt = reinterpret_cast<ReadInt>(Native(0x00416563));
const DWORD normalResume = Native(0x0094E2B9), normalReturn = Native(0x0094E45A);
const DWORD morphNative = Native(0x004520A4);
const DWORD levelResume = Native(0x007616FD);
const DWORD dispatchResume = Native(0x009678FF), meleeDispatch = Native(0x009690AE);
const DWORD permissionResume = Native(0x0092EE2C), permissionAccepted = Native(0x0092EE96);
const DWORD damageResume = Native(0x0078E6C2), damageStore = Native(0x0079059E);
bool installed = false;
LONG missingResourceLogged = 0;

template<typename Value> Value Field(const void* object, size_t offset) {
    return *reinterpret_cast<const Value*>(static_cast<const unsigned char*>(object) + offset);
}

void* LocalUser() { return *reinterpret_cast<void**>(Native(0x00BEBF98)); }

int MorphLevel(const void* user) {
    if (!user) return 0;
    const int morph = Field<int>(user, 0x528);
    return morph >= 64 && morph <= 67 ? morph - 63 : 0;
}

int SkillBase(int skill) {
    switch (skill) {
    case 1066: case 10001066: case 20001066: case 20011066: return 1066;
    case 1067: case 10001067: case 20001067: case 20011067: return 1067;
    default: return 0;
    }
}

int __cdecl InputSkill(void* user, int baseSkill) {
    if (!MorphLevel(user)) return 0;
    const auto methods = *reinterpret_cast<void***>(user);
    const int job = reinterpret_cast<GetJob>(methods[16])(user);
    if (job < 0 || job >= 3000) return 0;
    return (job / 100 == 22 || job == 2001 ? 20010000 : (job / 1000) * 10000000) + baseSkill;
}

int __cdecl Matches(void* user, int skill) {
    const int baseSkill = SkillBase(skill);
    return baseSkill && InputSkill(user, baseSkill) == skill;
}

bool LocalCharacter(const void* character) {
    const auto context = *reinterpret_cast<void**>(Native(0x00BE7918));
    return character && context && Field<void*>(context, 0x20B8) == character;
}

__declspec(naked) int __fastcall OriginalLevel(void*, void*, void*, int, void**) {
    __asm {
        push ebp
        mov ebp, esp
        push ecx
        mov [ebp-4], ecx
        jmp dword ptr [levelResume]
    }
}

int VisitorLevel(void* database, void* character, int skill, void** entry, bool casting) {
    if (!SkillBase(skill) || !LocalCharacter(character)) return -1;
    void* user = LocalUser();
    if (!Matches(user, skill)) return -1;
    void* resource = findSkill(database, skill);
    if (entry) *entry = resource;
    if (!resource) {
        if (InterlockedCompareExchange(&missingResourceLogged, 1, 0) == 0)
            ClientLog::Append(ClientLog::Component::Lifecycle,
                "event=visitors_morph_missing_resource skill=%d", skill);
        return 0;
    }
    return casting ? MorphLevel(user) : 1;
}

int __fastcall QueryLevel(void* database, void*, void* character, int skill, void** entry) {
    const int level = VisitorLevel(database, character, skill, entry, false);
    return level >= 0 ? level : OriginalLevel(database, nullptr, character, skill, entry);
}

int __fastcall CastingLevel(void* database, void*, void* character, int skill, void** entry) {
    const int level = VisitorLevel(database, character, skill, entry, true);
    return level >= 0 ? level : OriginalLevel(database, nullptr, character, skill, entry);
}

int __fastcall SkillKey(void* user, void*, int skill, int scanCode, void* result) {
    const int replacement = InputSkill(user, 1067);
    return replacement ? castSkill(user, replacement, 0, nullptr) : castSkill(user, skill, scanCode, result);
}

int __cdecl VisitorAction(void* user, void* entry, int level) {
    if (!entry || level < 1 || level > 4 || !Matches(user, Field<int>(entry, 0))) return -1;
    return SkillBase(Field<int>(entry, 0)) == 1066 ? 16 : 17;
}

int __cdecl ChooseAction(int category, int weapon, void* entry, int level, unsigned random) {
    const int action = VisitorAction(LocalUser(), entry, level);
    return action >= 0 ? action : selectAction(category, weapon, entry, level, random);
}

int __cdecl AcceptAction(void* user, void* entry, int level, int action) {
    return action >= 0 && VisitorAction(user, entry, level) == action;
}

int __cdecl FixedDamage(void* character, void* entry, int level, void* levelData) {
    if (!entry || !LocalCharacter(character) || level != MorphLevel(LocalUser())
        || !Matches(LocalUser(), Field<int>(entry, 0))) return -1;
    return readInt(static_cast<unsigned char*>(levelData) + 0xDC, Field<unsigned>(levelData, 0xE4));
}

__declspec(naked) void NormalInput() {
    __asm {
        pushfd
        pushad
        push 1066
        push esi
        call InputSkill
        add esp, 8
        test eax, eax
        jz original
        push 0
        push 0
        push eax
        mov ecx, esi
        call dword ptr [castSkill]
        popad
        popfd
        jmp dword ptr [normalReturn]
    original:
        popad
        popfd
        lea edi, [esi+88h]
        jmp dword ptr [normalResume]
    }
}

__declspec(naked) void MorphGate() {
    __asm {
        push ecx
        push edx
        push esi
        lea eax, [ecx-88h]
        push eax
        call Matches
        add esp, 8
        pop edx
        pop ecx
        test eax, eax
        jz original
        xor eax, eax
        ret
    original:
        jmp dword ptr [morphNative]
    }
}

__declspec(naked) void Dispatch() {
    static const DWORD shuttleDispatch = Native(0x009691AC);
    __asm {
        pushfd
        pushad
        cmp esi, 1065
        je shuttle
        cmp esi, 10001065
        je shuttle
        cmp esi, 20001065
        je shuttle
        cmp esi, 20011065
        je shuttle
        push esi
        push ebx
        call Matches
        add esp, 8
        test eax, eax
        jz original
        popad
        popfd
        jmp dword ptr [meleeDispatch]
    original:
        popad
        popfd
        cmp esi, 407h
        jmp dword ptr [dispatchResume]
    shuttle:
        popad
        popfd
        jmp dword ptr [shuttleDispatch]
    }
}

__declspec(naked) void ActionPermission() {
    __asm {
        pushfd
        pushad
        push edi
        push dword ptr [ebp+14h]
        push dword ptr [ebp+10h]
        push esi
        call AcceptAction
        add esp, 10h
        test eax, eax
        jz original
        popad
        popfd
        jmp dword ptr [permissionAccepted]
    original:
        popad
        popfd
        mov ecx, [ebp+10h]
        test ecx, ecx
        jmp dword ptr [permissionResume]
    }
}

__declspec(naked) void Damage() {
    __asm {
        pushfd
        pushad
        push ebx
        push dword ptr [ebp+3ch]
        push dword ptr [ebp+38h]
        push dword ptr [ebp+8]
        call FixedDamage
        add esp, 10h
        test eax, eax
        js original
        mov [esp+1ch], eax
        popad
        popfd
        jmp dword ptr [damageStore]
    original:
        popad
        popfd
        push dword ptr [ebx+0e4h]
        jmp dword ptr [damageResume]
    }
}

struct Patch {
    DWORD address;
    const char* expected;
    size_t size;
    void* target;
    unsigned char opcode;
    DWORD protection;
};

Patch patches[] = {
    {Native(0x0094E2B3), "\x8d\xbe\x88\x00\x00\x00", 6, NormalInput, 0xE9, 0},
    {Native(0x0094FA28), "\xe8\x4d\x75\x01\x00", 5, SkillKey, 0xE8, 0},
    {Native(0x0096754D), "\xe8\x52\xab\xae\xff", 5, MorphGate, 0xE8, 0},
    {Native(0x007616F6), "\x55\x8b\xec\x51\x89\x4d\xfc", 7, QueryLevel, 0xE9, 0},
    {Native(0x009675D5), "\xe8\x1c\xa1\xdf\xff", 5, CastingLevel, 0xE8, 0},
    {Native(0x009678F9), "\x81\xfe\x07\x04\x00\x00", 6, Dispatch, 0xE9, 0},
    {Native(0x00950F11), "\xe8\x1b\x51\xe1\xff", 5, ChooseAction, 0xE8, 0},
    {Native(0x0092EE27), "\x8b\x4d\x10\x85\xc9", 5, ActionPermission, 0xE9, 0},
    {Native(0x0078E6BC), "\xff\xb3\xe4\x00\x00\x00", 6, Damage, 0xE9, 0}
};

bool RestoreProtection(size_t count) {
    bool restored = true;
    while (count) {
        auto& patch = patches[--count];
        DWORD ignored;
        if (!VirtualProtect(reinterpret_cast<void*>(patch.address), patch.size, patch.protection, &ignored))
            restored = false;
    }
    return restored;
}
}

bool VisitorsMorph::Install(HMODULE module) {
    if (reinterpret_cast<DWORD>(module) != Native(0x00400000)) return false;
    if (installed) return true;
    const auto* base = reinterpret_cast<const unsigned char*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew > 0x1000) return false;
    const auto* image = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (image->Signature != IMAGE_NT_SIGNATURE || image->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || image->FileHeader.TimeDateStamp != 0x4B7C15C9 || image->OptionalHeader.SizeOfImage != 0xA94000) return false;
    for (const auto& patch : patches) {
        const auto* site = reinterpret_cast<const unsigned char*>(patch.address);
        if (std::memcmp(site, patch.expected, patch.size)) {
            char expected[32]{}, actual[32]{};
            for (size_t index = 0; index < patch.size; ++index) {
                std::snprintf(expected + index * 3, 4, "%02X ", static_cast<unsigned char>(patch.expected[index]));
                std::snprintf(actual + index * 3, 4, "%02X ", site[index]);
            }
            ClientLog::Append(ClientLog::Component::Lifecycle,
                "event=visitors_morph_install success=0 reason=signature address=%08lX expected=%s actual=%s",
                patch.address, expected, actual);
            return false;
        }
    }
    size_t acquired = 0;
    for (auto& patch : patches) {
        if (!VirtualProtect(reinterpret_cast<void*>(patch.address), patch.size, PAGE_EXECUTE_READWRITE, &patch.protection)) {
            const DWORD error = GetLastError();
            const bool restored = RestoreProtection(acquired);
            ClientLog::Append(ClientLog::Component::Lifecycle,
                "event=visitors_morph_install success=0 reason=protect address=%08lX error=%lu protectionRestored=%d",
                patch.address, error, restored);
            return false;
        }
        ++acquired;
    }
    for (const auto& patch : patches) {
        auto* site = reinterpret_cast<unsigned char*>(patch.address);
        const DWORD displacement = reinterpret_cast<DWORD>(patch.target) - patch.address - 5;
        std::memset(site, 0x90, patch.size);
        site[0] = patch.opcode;
        std::memcpy(site + 1, &displacement, sizeof(displacement));
    }
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), base, image->OptionalHeader.SizeOfImage) != FALSE;
    if (!flushed) {
        for (const auto& patch : patches)
            std::memcpy(reinterpret_cast<void*>(patch.address), patch.expected, patch.size);
        const bool rollbackFlushed = FlushInstructionCache(GetCurrentProcess(), base, image->OptionalHeader.SizeOfImage) != FALSE;
        ClientLog::Append(ClientLog::Component::Lifecycle,
            "event=visitors_morph_install success=0 reason=flush rollbackFlushed=%d", rollbackFlushed);
    }
    const bool restored = RestoreProtection(acquired);
    installed = flushed;
    ClientLog::Append(ClientLog::Component::Lifecycle,
        "event=visitors_morph_install version=1 success=%d patches=9 protectionRestored=%d", installed, restored);
    return installed;
}
