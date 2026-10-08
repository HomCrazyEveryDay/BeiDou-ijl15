#include "stdafx.h"
#include "MobInitializationFix.h"
#include "CrashReporter.h"
#include <cstring>

namespace {
DWORD resume, inactive, ownerReturn, vectorReturn, spawnReturn;
BYTE* installedClient = nullptr;

// CMob+4 is the IMovePathOwner subobject. During CMob::Init the first
// CVecCtrlMob calls it before CMob+11C (the second vector) is constructed.
// The second vector's +250 flag starts at zero (009BBD5D). Only this exact
// initialization chain may use that initial value without reading it.
__declspec(naked) void SelectInitialAction() {
    __asm {
        push ebp
        mov ebp, esp
        mov eax, [ecx+118h]
        test eax, eax
        jnz original
        push eax
        mov eax, [ebp+4]
        cmp eax, dword ptr [ownerReturn]
        jne notInitializing
        mov eax, [ebp]
        test eax, eax
        jz notInitializing
        mov eax, [eax+4]
        cmp eax, dword ptr [vectorReturn]
        jne notInitializing
        mov eax, [ebp]
        mov eax, [eax]
        test eax, eax
        jz notInitializing
        mov eax, [eax+4]
        cmp eax, dword ptr [spawnReturn]
        jne notInitializing
        mov eax, [ecx+114h]
        test eax, eax
        jz notInitializing
        sub eax, 0ch
        cmp eax, [ebp+14h]
        jne notInitializing
        pop eax
        // Match the skipped native prologue's saved registers and null
        // conversion, then retain all movement/template/action selection.
        sub eax, 0ch
        xor edx, edx
        push esi
        push edi
        xor edi, edi
        jmp dword ptr [inactive]
    notInitializing:
        pop eax
    original:
        jmp dword ptr [resume]
    }
}
}

bool MobInitializationFix::Install(HMODULE module) {
    auto* base = reinterpret_cast<BYTE*>(module);
    if (installedClient) return base == installedClient;
    if (!base) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew > 0x1000) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || nt->FileHeader.TimeDateStamp != 0x4B7C15C9 || nt->OptionalHeader.SizeOfImage != 0xA94000) return false;
    const BYTE entry[] = {0x55,0x8b,0xec,0x8b,0x81,0x18,0x01,0,0};
    const BYTE fault[] = {0x39,0xba,0x50,0x02,0,0,0x74,0x25};
    const BYTE zero[] = {0x33,0xf6,0x39,0xb9,0xa0,0x02,0,0};
    const BYTE owner[] = {0xff,0x50,0x04,0x89,0x45,0x1c};
    const BYTE vector[] = {0xe8,0x4b,0x55,0xff,0xff,0x33,0xc0,0x6a,0x14,0x89,0x86,0x50,0x02,0,0};
    const BYTE spawn[] = {0xff,0x52,0x04,0x8b,0x45,0xe4};
    const BYTE second[] = {0xe8,0x6a,0x92,0x35,0,0x89,0x45,0xd8,0x8d,0x45,0xd8,0x8d,0x8b,0x1c,0x01,0,0};
    if (memcmp(base+0x26b599,entry,sizeof(entry)) || memcmp(base+0x26b5b1,fault,sizeof(fault))
        || memcmp(base+0x26b5de,zero,sizeof(zero)) || memcmp(base+0x5b13e4,owner,sizeof(owner))
        || memcmp(base+0x5bbd58,vector,sizeof(vector)) || memcmp(base+0x262a2f,spawn,sizeof(spawn))
        || memcmp(base+0x262a55,second,sizeof(second))) return false;
    resume = reinterpret_cast<DWORD>(base+0x26b5a2);
    inactive = reinterpret_cast<DWORD>(base+0x26b5de);
    ownerReturn = reinterpret_cast<DWORD>(base+0x5b13e7);
    vectorReturn = reinterpret_cast<DWORD>(base+0x5bbd5d);
    spawnReturn = reinterpret_cast<DWORD>(base+0x262a32);
    auto* site = base+0x26b599;
    DWORD protection = 0;
    if (!VirtualProtect(site,sizeof(entry),PAGE_EXECUTE_READWRITE,&protection)) return false;
    const DWORD distance = reinterpret_cast<DWORD>(&SelectInitialAction)-reinterpret_cast<DWORD>(site)-5;
    site[0]=0xe9;
    memcpy(site+1,&distance,sizeof(distance));
    memset(site+5,0x90,sizeof(entry)-5);
    const bool flushed = FlushInstructionCache(GetCurrentProcess(),site,sizeof(entry)) != FALSE;
    DWORD ignored=0;
    const bool restored = VirtualProtect(site,sizeof(entry),protection,&ignored) != FALSE;
    installedClient=base;
    CrashReporter::RecordEvent("mob.init.fix","install result=1 flush=%d protectionRestored=%d",flushed,restored);
    return true;
}
