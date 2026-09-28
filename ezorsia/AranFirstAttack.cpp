#include "stdafx.h"
#include "AranFirstAttack.h"
#include <cstring>

namespace {
#ifdef ARAN_FIRST_ATTACK_TEST
constexpr DWORD Native(DWORD address) { return address + 0x10000000; }
#else
constexpr DWORD Native(DWORD address) { return address; }
#endif
constexpr DWORD kRangeSite = Native(0x00951417);
const DWORD kFacingRight = Native(0x0095141D);
const DWORD kFacingLeft = Native(0x00951433);
bool g_installed = false;

// CUserLocal's melee attack has already built the action/weapon rectangle.
// EAX = left, EDX = right; the rectangle still faces left at this point.
// Change only skill 0 for an advanced Aran using a polearm. The original
// facing code then mirrors the result before CMobPool selects the targets.
__declspec(naked) void ExtendFirstAttack() {
    __asm {
        pushad
        cmp dword ptr [ebp-10h], 0
        jne original
        cmp dword ptr [ebp-4Ch], 44
        jne original
        cmp dword ptr [ebp-6Ch], -180
        jle original
        mov eax, [ebx]
        mov ecx, ebx
        call dword ptr [eax+40h]
        cmp eax, 2100
        je extendRange
        cmp eax, 2110
        jl original
        cmp eax, 2112
        jg original
    extendRange:
        mov dword ptr [ebp-6Ch], -180
    original:
        popad
        mov eax, [ebp-6Ch]
        cmp dword ptr [ebp-18h], 0
        jne facingLeft
        jmp dword ptr [kFacingRight]
    facingLeft:
        jmp dword ptr [kFacingLeft]
    }
}
}

bool AranFirstAttack::Install() {
    if (g_installed) return true;
    constexpr unsigned char expected[] = {0x83, 0x7d, 0xe8, 0x00, 0x75, 0x16};
    auto* site = reinterpret_cast<unsigned char*>(kRangeSite);
    if (std::memcmp(site, expected, sizeof(expected)) != 0) return false;
    DWORD previous;
    if (!VirtualProtect(site, sizeof(expected), PAGE_EXECUTE_READWRITE, &previous)) return false;
    unsigned char patch[] = {0xe9, 0, 0, 0, 0, 0x90};
    const DWORD relative = reinterpret_cast<DWORD>(&ExtendFirstAttack) - (kRangeSite + 5);
    std::memcpy(patch + 1, &relative, sizeof(relative));
    std::memcpy(site, patch, sizeof(patch));
    FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch));
    DWORD ignored;
    VirtualProtect(site, sizeof(patch), previous, &ignored);
    g_installed = true;
    return true;
}
