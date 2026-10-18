#include "stdafx.h"
#include "PetPotionPersistence.h"
#include <cstring>

namespace {
BYTE* installedClient = nullptr;
void** localUser = nullptr;
using SetPotion = void(__thiscall*)(void*, int);
SetPotion setHp = nullptr, setMp = nullptr;

bool HasLeadPet() {
    auto user = localUser ? static_cast<BYTE*>(*localUser) : nullptr;
    auto pets = user ? *reinterpret_cast<BYTE**>(user + 0x1edc) : nullptr;
    return pets && *reinterpret_cast<void**>(pets + 4);
}
void __fastcall ClearHpIfActive(void* keymap, void*) {
    if (HasLeadPet()) setHp(keymap, 0);
}
void __fastcall ClearMpIfActive(void* keymap, void*) {
    if (HasLeadPet()) setMp(keymap, 0);
}
}

bool PetPotionPersistence::Install(HMODULE client) {
    auto base = reinterpret_cast<BYTE*>(client);
    if (installedClient) return installedClient == base;
    if (!base) return false;
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew > 0x1000) return false;
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || nt->FileHeader.TimeDateStamp != 0x4B7C15C9 || nt->OptionalHeader.SizeOfImage != 0xA94000) return false;

    // CUIPetEquip::Draw (083): after drawing each disabled slot, push 0 and
    // call CFuncKeyMappedMan's setter, which also sends CHANGE_KEYMAP to the server.
    // Skip these clears only while the lead pet is absent. Keep native disabled
    // shading, capability loss on an active pet, explicit removal and login init.
    const BYTE hpClear[] = {0x53, 0xe8, 0xfb, 0xc9, 0xd8, 0xff};
    const BYTE mpClear[] = {0x53, 0xe8, 0xf9, 0xc9, 0xd8, 0xff};
    const BYTE hpCheck[] = {0x3b, 0xc3, 0x59, 0x59, 0x75, 0x45};
    const BYTE mpCheck[] = {0x3b, 0xc3, 0x59, 0x59, 0x75, 0x45};
    if (std::memcmp(base + 0x40163b, hpClear, sizeof(hpClear))
        || std::memcmp(base + 0x4016a9, mpClear, sizeof(mpClear))
        || std::memcmp(base + 0x4015f6, hpCheck, sizeof(hpCheck))
        || std::memcmp(base + 0x401664, mpCheck, sizeof(mpCheck))) return false;

    // Both sites are on the same page. Acquire it before changing either slot.
    constexpr SIZE_T span = 0x4016af - 0x40163b;
    DWORD protection = 0, ignored = 0;
    if (!VirtualProtect(base + 0x40163b, span, PAGE_EXECUTE_READWRITE, &protection)) return false;
    localUser = reinterpret_cast<void**>(base + 0x7ebf98);
    setHp = reinterpret_cast<SetPotion>(base + 0x18e03c);
    setMp = reinterpret_cast<SetPotion>(base + 0x18e0a8);
    const auto replace = [](BYTE* site, void* target) {
        // Replace push ebx; call setter with call callback; nop. Both leave ESP unchanged.
        site[0] = 0xe8;
        const DWORD distance = reinterpret_cast<DWORD>(target) - reinterpret_cast<DWORD>(site) - 5;
        std::memcpy(site + 1, &distance, sizeof(distance));
        site[5] = 0x90;
    };
    replace(base + 0x40163b, ClearHpIfActive);
    replace(base + 0x4016a9, ClearMpIfActive);
    const bool ok = FlushInstructionCache(GetCurrentProcess(), base + 0x40163b, span) != FALSE;
    if (!ok) {
        std::memcpy(base + 0x40163b, hpClear, sizeof(hpClear));
        std::memcpy(base + 0x4016a9, mpClear, sizeof(mpClear));
        FlushInstructionCache(GetCurrentProcess(), base + 0x40163b, span);
    }
    VirtualProtect(base + 0x40163b, span, protection, &ignored);
    if (ok) installedClient = base;
    return ok;
}
