#include "stdafx.h"
#include "InventoryRefreshFix.h"
#include "CrashReporter.h"
#include <cstring>

namespace {
DWORD readItemId, nextOperation;
BYTE* installedClient = nullptr;
volatile LONG recoveries = 0;

void __cdecl RecordMissingCashItem(int slot) {
    if (InterlockedIncrement(&recoveries) <= 8)
        CrashReporter::RecordEvent("inventory.refresh", "empty cash removal slot=%d; continue packet", slot);
}

// OnInventoryOperation mode=3 has already consumed type/slot and released
// its temporary ZRef. A CASH refresh can remove an already empty slot before
// adding the authoritative item in the SAME packet. Do not return from the
// handler: its native loop must still decode every subsequent operation and
// perform the normal inventory/UI finalization.
__declspec(naked) void RemoveCashItemIfPresent() {
    __asm {
        mov eax, [ebp-10h]
        pushfd
        test eax, eax
        jnz original
        cmp dword ptr [ebp-18h], 5
        jne original
        test edi, edi
        jle original
        pushad
        push edi
        call RecordMissingCashItem
        add esp, 4
        popad
        popfd
        jmp dword ptr [nextOperation]
    original:
        popfd
        lea ecx, [eax+0ch]
        jmp dword ptr [readItemId]
    }
}
}

bool InventoryRefreshFix::Install(HMODULE client) {
    auto* base = reinterpret_cast<BYTE*>(client);
    if (installedClient) return base == installedClient;
    if (!base) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew > 0x1000) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || nt->FileHeader.TimeDateStamp != 0x4B7C15C9 || nt->OptionalHeader.SizeOfImage != 0xA94000) return false;
    const BYTE lookup[] = {0xe8,0x1f,0x96,0xa0,0xff,0x39,0x5d,0xa4,0x8b,0x40,0x04,0x89,0x45,0xf0};
    const BYTE entry[] = {0x8b,0x45,0xf0,0x8d,0x48,0x0c,0xe8,0x43,0x9a,0xa0,0xff};
    const BYTE loop[] = {0xff,0x4d,0xd8,0x0f,0x85,0xee,0xfa,0xff,0xff,0x39,0x5d,0xdc};
    if (memcmp(base+0x61ecd3,lookup,sizeof(lookup)) || memcmp(base+0x61ecef,entry,sizeof(entry))
        || memcmp(base+0x61f13e,loop,sizeof(loop))) return false;
    readItemId = reinterpret_cast<DWORD>(base+0x61ecf5);
    nextOperation = reinterpret_cast<DWORD>(base+0x61f13e);
    auto* site = base+0x61ecef;
    DWORD protection = 0;
    if (!VirtualProtect(site,6,PAGE_EXECUTE_READWRITE,&protection)) return false;
    const DWORD distance = reinterpret_cast<DWORD>(&RemoveCashItemIfPresent)-reinterpret_cast<DWORD>(site)-5;
    site[0]=0xe9;
    memcpy(site+1,&distance,sizeof(distance));
    site[5]=0x90;
    const bool flushed = FlushInstructionCache(GetCurrentProcess(),site,6) != FALSE;
    DWORD ignored=0;
    const bool restored = VirtualProtect(site,6,protection,&ignored) != FALSE;
    installedClient=base;
    CrashReporter::RecordEvent("inventory.refresh", "install result=1 flush=%d protectionRestored=%d",flushed,restored);
    return true;
}
