#include "stdafx.h"
#include "PetEquipScrollCompatibility.h"
#include "CrashReporter.h"
#include <cstring>

namespace {
BYTE* installedClient = nullptr;
DWORD accepted, nativeCategoryCheck;

// 004F5497 is the shared scroll/equipment category predicate. Its chaos and
// clean-slate branch excludes all 18xxxxx pet equipment at 004F54FE. EDI is
// the scroll ID, EBX the equipment ID, EAX equipment ID / 100000. Only the
// two supported chaos scrolls may bypass that exclusion for these outfits.
__declspec(naked) void AllowPetChaosScroll() {
    __asm {
        cmp eax, 18
        jne allow
        cmp edi, 2049100
        je checkOutfit
        cmp edi, 2049115
        jne original
    checkOutfit:
        cmp ebx, 1802896
        je allow
        cmp ebx, 1802897
        je allow
        cmp ebx, 1802899
        je allow
    original:
        jmp dword ptr [nativeCategoryCheck]
    allow:
        jmp dword ptr [accepted]
    }
}
}

bool PetEquipScrollCompatibility::Install(HMODULE client) {
    auto* base = reinterpret_cast<BYTE*>(client);
    if (installedClient) return base == installedClient;
    if (!base) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew > 0x1000) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || nt->FileHeader.TimeDateStamp != 0x4B7C15C9 || nt->OptionalHeader.SizeOfImage != 0xA94000) return false;
    const BYTE entry[] = {0x53,0x56,0x57,0x8b,0x7c,0x24,0x10,0x8b,0xc7};
    const BYTE exclusion[] = {0x8b,0xc3,0x55,0x99,0xbd,0xa0,0x86,0x01,0x00,0xf7,0xfd,0x5d,
        0x83,0xf8,0x12,0x75,0x2e,0x8b,0xc7,0x99};
    const BYTE success[] = {0x6a,0x01,0x58,0xeb,0x0b};
    if (memcmp(base+0xf5497,entry,sizeof(entry)) || memcmp(base+0xf54f2,exclusion,sizeof(exclusion))
        || memcmp(base+0xf5531,success,sizeof(success))) return false;

    auto* site = base + 0xf54fe;
    DWORD protection = 0;
    if (!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&protection)) return false;
    accepted = reinterpret_cast<DWORD>(base + 0xf5531);
    nativeCategoryCheck = reinterpret_cast<DWORD>(base + 0xf5503);
    const DWORD distance = reinterpret_cast<DWORD>(&AllowPetChaosScroll)-reinterpret_cast<DWORD>(site)-5;
    site[0] = 0xe9;
    memcpy(site+1,&distance,sizeof(distance));
    const bool flushed = FlushInstructionCache(GetCurrentProcess(),site,5) != FALSE;
    DWORD ignored = 0;
    const bool restored = VirtualProtect(site,5,protection,&ignored) != FALSE;
    installedClient = base;
    CrashReporter::RecordEvent("pet.scroll", "install result=1 flush=%d protectionRestored=%d",flushed,restored);
    return true;
}
