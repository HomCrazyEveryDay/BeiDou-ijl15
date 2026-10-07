#include "stdafx.h"
#include "ComboTempestCritical.h"
#include "EquipmentCritical.h"
#include <cstring>

namespace {
#ifdef COMBO_TEMPEST_CRITICAL_TEST
constexpr DWORD Native(DWORD address) { return address + 0x10000000; }
#else
constexpr DWORD Native(DWORD address) { return address; }
#endif
constexpr DWORD kFooterSite = Native(0x00955435);
const DWORD kFooterNative = Native(0x006711AC);
const DWORD kFooterReturn = Native(0x0095543C);
bool g_installed = false;

// The ranged target footer was ignored by the server. Preserve packet length
// and damage magnitudes; AC01 carries the native per-line critical decisions.
unsigned __cdecl Footer(int skill, int count, const int* critical, unsigned original) {
    if (EquipmentCritical::Active()) return EquipmentCritical::Footer(count, critical, original);
    if (skill != 21120006 || count < 1 || count > 15) return original;
    unsigned result = 0xac010000;
    for (int i = 0; i < count; ++i) if (critical[i]) result |= 1u << i;
    return result;
}

__declspec(naked) void RangedCriticalFooter() {
    __asm {
        mov ecx, [esi]
        call dword ptr [kFooterNative]
        pushfd
        push ecx
        push edx
        push eax
        lea eax, [esi+54h]
        push eax
        push dword ptr [ebp-18h]
        push dword ptr [ebp-10h]
        call Footer
        add esp, 10h
        pop edx
        pop ecx
        popfd
        jmp dword ptr [kFooterReturn]
    }
}
}

bool ComboTempestCritical::Install() {
    if (g_installed) return true;
    constexpr unsigned char expected[] = {0x8b, 0x0e, 0xe8, 0x70, 0xbd, 0xd1, 0xff};
    auto* site = reinterpret_cast<unsigned char*>(kFooterSite);
    if (std::memcmp(site, expected, sizeof(expected)) != 0) return false;
    DWORD previous;
    if (!VirtualProtect(site, sizeof(expected), PAGE_EXECUTE_READWRITE, &previous)) return false;
    unsigned char patch[] = {0xe9, 0, 0, 0, 0, 0x90, 0x90};
    const DWORD relative = reinterpret_cast<DWORD>(&RangedCriticalFooter) - (kFooterSite + 5);
    std::memcpy(patch + 1, &relative, sizeof(relative));
    std::memcpy(site, patch, sizeof(patch));
    FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch));
    DWORD ignored;
    VirtualProtect(site, sizeof(patch), previous, &ignored);
    g_installed = true;
    return true;
}
