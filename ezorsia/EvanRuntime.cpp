#include "stdafx.h"
#include "EvanRuntime.h"
#include <cstring>

namespace {
#ifdef EVAN_RUNTIME_TEST
constexpr DWORD Native(DWORD address) { return address + 0x10000000; }
#else
constexpr DWORD Native(DWORD address) { return address; }
#endif
// v84 treats 5620006..8 as mastery books (0x4F959A). Keep the v83
// native skill-book packet/lock lifecycle and accept their CASH slots.
const DWORD bookClickReturn = Native(0x004F06CB);
const DWORD bookSendReturn = Native(0x00A0A1D2);
__declspec(naked) void BookClick() {
    __asm {
        cmp eax, 562
        jne original
        cmp edx, 6
        jl original
        cmp edx, 8
        jg original
        cmp eax, eax
        jmp dword ptr [bookClickReturn]
    original:
        cmp eax, 228
        jmp dword ptr [bookClickReturn]
    }
}
__declspec(naked) void BookSend() {
    __asm {
        cmp eax, 562
        jne original
        cmp edx, 6
        jl original
        cmp edx, 8
        jg original
        cmp eax, eax
        jmp dword ptr [bookSendReturn]
    original:
        cmp eax, 228
        jmp dword ptr [bookSendReturn]
    }
}
// GMS 084 0x4F0AD2 adds these three cash-book skills to the shared
// master-level predicate. Pair with the server's skill-list field predicate.
const DWORD masteryReturn = Native(0x004E8F09);
__declspec(naked) void EvanMastery() {
    __asm {
        mov eax, [esp+4]
        cmp eax, 22111001
        je hasMastery
        cmp eax, 22141002
        je hasMastery
        cmp eax, 22140000
        je hasMastery
        cdq
        jmp dword ptr [masteryReturn]
    hasMastery:
        mov eax, 1
        ret
    }
}
// v84 Blaze uses the projectile/chain branch (0x992D53), not the
// rectangular lt/rb branch. The v83 prototype ID is now Flame Wheel:
// replace it with Blaze, rather than admitting both into the ball branch.
const DWORD blazeChainReturn = Native(0x00955EE4);
const DWORD blazeBallReturn = Native(0x00955F25);
const DWORD blazeOriginReturn = Native(0x00955FB8);
__declspec(naked) void BlazeChain() {
    __asm {
        cmp dword ptr [ebp-14h], 22181001
        jmp dword ptr [blazeChainReturn]
    }
}
__declspec(naked) void BlazeBall() {
    __asm {
        cmp dword ptr [ebp-14h], 22181001
        jmp dword ptr [blazeBallReturn]
    }
}
__declspec(naked) void BlazeOrigin() {
    __asm {
        cmp dword ptr [ebp-14h], 22181001
        jmp dword ptr [blazeOriginReturn]
    }
}
// Both lookup functions explicitly discard the 22xx and 2001 skill families.
// Keep the native skill map, level checks and all non-Evan handling intact.
struct Patch {
    DWORD address;
    unsigned char before[8];
    unsigned char after[8];
    size_t size;
};
Patch patches[] = {
    {Native(0x00761717), {0x0f,0x84,0xd7,0,0,0}, {0x90,0x90,0x90,0x90,0x90,0x90}, 6},
    {Native(0x00761723), {0x0f,0x84,0xcb,0,0,0}, {0x90,0x90,0x90,0x90,0x90,0x90}, 6},
    {Native(0x0075c779), {0x74,0x08}, {0x90,0x90}, 2},
    {Native(0x0075c781), {0x75,0x04}, {0xeb,0x04}, 2},
    {Native(0x00955E22), {0x89,0x74,0x52,0x01}, {0x7b,0x4d,0x52,0x01}, 4},
    {Native(0x00955EDD), {0x81,0x7d,0xec,0x7b,0x4d,0x52,0x01}, {0xe9,0,0,0,0,0x90,0x90}, 7},
    {Native(0x00955F1E), {0x81,0x7d,0xec,0x7b,0x4d,0x52,0x01}, {0xe9,0,0,0,0,0x90,0x90}, 7},
    {Native(0x00955FB1), {0x81,0x7d,0xec,0x7b,0x4d,0x52,0x01}, {0xe9,0,0,0,0,0x90,0x90}, 7},
    {Native(0x004E8F04), {0x8b,0x44,0x24,0x04,0x99}, {0xe9,0,0,0,0}, 5},
    {Native(0x004F06C6), {0x3d,0xe4,0,0,0}, {0xe9,0,0,0,0}, 5},
    {Native(0x00A0A1CD), {0x3d,0xe4,0,0,0}, {0xe9,0,0,0,0}, 5}
};
}

bool EvanRuntime::Install() {
    const DWORD bookTargets[] = {reinterpret_cast<DWORD>(&EvanMastery), reinterpret_cast<DWORD>(&BookClick), reinterpret_cast<DWORD>(&BookSend)};
    for (size_t i=0;i<3;++i) {
        const DWORD displacement = bookTargets[i] - (patches[8+i].address + 5);
        std::memcpy(patches[8+i].after+1, &displacement, sizeof(displacement));
    }
    const unsigned char auraDispatch[] = {0xc7,0x86,0x74,0x05,0,0,0x97,0,0,0};
    if (std::memcmp(reinterpret_cast<void*>(Native(0x0096CA98)), auraDispatch, sizeof(auraDispatch))) return false;
    const DWORD destinations[] = {reinterpret_cast<DWORD>(&BlazeChain), reinterpret_cast<DWORD>(&BlazeBall), reinterpret_cast<DWORD>(&BlazeOrigin)};
    for (size_t i=0;i<3;++i) {
        const DWORD displacement = destinations[i] - (patches[5+i].address + 5);
        std::memcpy(patches[5+i].after+1, &displacement, sizeof(displacement));
    }
    for (const auto& patch : patches) {
        if (std::memcmp(reinterpret_cast<const void*>(patch.address), patch.before, patch.size)) return false;
    }
    // Acquire every page before modifying any instruction, so a protection
    // failure cannot leave one skill lookup enabled and the other disabled.
    DWORD previous[sizeof(patches) / sizeof(patches[0])]{};
    size_t acquired = 0;
    for (const auto& patch : patches) {
        if (!VirtualProtect(reinterpret_cast<void*>(patch.address), patch.size,
                PAGE_EXECUTE_READWRITE, &previous[acquired])) {
            for (size_t i = acquired; i > 0; --i) {
                DWORD ignored;
                VirtualProtect(reinterpret_cast<void*>(patches[i-1].address), patches[i-1].size, previous[i-1], &ignored);
            }
            return false;
        }
        ++acquired;
    }
    for (const auto& patch : patches) {
        std::memcpy(reinterpret_cast<void*>(patch.address), patch.after, patch.size);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(patch.address), patch.size);
    }
    for (size_t i = acquired; i > 0; --i) {
        DWORD ignored;
        VirtualProtect(reinterpret_cast<void*>(patches[i-1].address), patches[i-1].size, previous[i-1], &ignored);
    }
    return true;
}
