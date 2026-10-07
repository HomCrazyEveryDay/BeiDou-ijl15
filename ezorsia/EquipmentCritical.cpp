#include "stdafx.h"
#include "EquipmentCritical.h"
#include <cstring>

namespace {
#ifdef EQUIPMENT_CRITICAL_TEST
constexpr DWORD Native(DWORD address) { return address + 0x10000000; }
#else
constexpr DWORD Native(DWORD address) { return address; }
#endif
const DWORD physicalReturn = Native(0x0078E370);
const DWORD magicReturn = Native(0x00791853);
const DWORD footerNative = Native(0x006711AC);
const DWORD meleeReturn = Native(0x00952B3D);
bool installed = false;

void __stdcall Physical(int* frame) {
    EquipmentCritical::Add(frame[-0x40 / 4], frame[-0x34 / 4], false);
    // The original physical gate recognizes only skill/buff sources.
    if ((EquipmentCritical::Snapshot().load() & 0xff) != 0) frame[-0x80 / 4] = 1;
}
void __stdcall Magic(int* frame) {
    EquipmentCritical::Add(frame[0x0c / 4], frame[-0x24 / 4], true);
}
unsigned __cdecl Footer(int count, const int* critical, unsigned original) {
    return EquipmentCritical::Footer(count, critical, original);
}
__declspec(naked) void PhysicalHook() {
    __asm {
        pushad
        push ebp
        call Physical
        popad
        xor edi, edi
        cmp dword ptr [ebp-28h], 3ee1b0h
        jmp dword ptr [physicalReturn]
    }
}
__declspec(naked) void MagicHook() {
    __asm {
        pushad
        push ebp
        call Magic
        popad
        mov ebx, [ebp-24h]
        mov ecx, [ebp+1ch]
        test ecx, ecx
        jmp dword ptr [magicReturn]
    }
}
__declspec(naked) void MeleeFooter() {
    __asm {
        mov ecx, [edi]
        call dword ptr [footerNative]
        pushfd
        push ecx
        push edx
        push eax
        lea eax, [edi+54h]
        push eax
        push dword ptr [ebp-58h]
        call Footer
        add esp, 0ch
        pop edx
        pop ecx
        popfd
        jmp dword ptr [meleeReturn]
    }
}
}

bool EquipmentCritical::Install() {
    if (installed) return true;
    struct Patch { DWORD address; const char* bytes; size_t size; void* hook; DWORD protection; };
    Patch patches[] = {
        {Native(0x0078E367), "\x33\xff\x81\x7d\xd8\xb0\xe1\x3e\x00", 9, PhysicalHook, 0},
        {Native(0x0079184E), "\x8b\x4d\x1c\x85\xc9", 5, MagicHook, 0},
        {Native(0x00952B36), "\x8b\x0f\xe8\x6f\xe6\xd1\xff", 7, MeleeFooter, 0}
    };
    for (const auto& patch : patches)
        if (std::memcmp(reinterpret_cast<void*>(patch.address), patch.bytes, patch.size)) return false;
    for (size_t i = 0; i < 3; ++i) {
        auto& patch = patches[i];
        if (!VirtualProtect(reinterpret_cast<void*>(patch.address), patch.size, PAGE_EXECUTE_READWRITE, &patch.protection)) {
            for (size_t j = 0; j < i; ++j) {
                DWORD ignored;
                VirtualProtect(reinterpret_cast<void*>(patches[j].address), patches[j].size, patches[j].protection, &ignored);
            }
            return false;
        }
    }
    for (const auto& patch : patches) {
        auto* site = reinterpret_cast<unsigned char*>(patch.address);
        std::memset(site, 0x90, patch.size);
        site[0] = 0xe9;
        const DWORD offset = reinterpret_cast<DWORD>(patch.hook) - patch.address - 5;
        std::memcpy(site + 1, &offset, 4);
        FlushInstructionCache(GetCurrentProcess(), site, patch.size);
        DWORD ignored;
        VirtualProtect(site, patch.size, patch.protection, &ignored);
    }
    installed = true;
    return true;
}
