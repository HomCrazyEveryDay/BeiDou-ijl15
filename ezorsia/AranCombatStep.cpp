#include "stdafx.h"
#include "AranCombatStep.h"
#include <cstring>

namespace {
using CastSkill = int(__thiscall*)(void*, const int*, int);
using SetImpact = void(__thiscall*)(void*, double, double);
using GetRope = void*(__cdecl*)(const void*, unsigned);
CastSkill nativeCast;
SetImpact nativeImpact;
GetRope nativeGetRope;
DWORD attachContinue;
bool installed = false;

// Ground WZ impulse is reduced by sqrt(2/3) for the shorter stopping distance.
// Apply that factor once more in air: horizontal air travel scales with speed,
// so its resulting impulse becomes 2/3 of the previous airborne value.
constexpr double kAirHorizontalScale = 0.408248290463863; // 0.5 * sqrt(2/3)
struct AirUse {
    void* user = nullptr;
    unsigned char* controller = nullptr;
    bool used = false;
} airUse;

bool Grounded(const unsigned char* controller) {
    return *reinterpret_cast<void* const*>(controller + 0x110) != nullptr;
}

int __fastcall Cast(void* user, void*, const int* skill, int level) {
    if (!user || !skill || *skill != 21001001 || level <= 0) return 0;
    auto* vector = *reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(user) + 0x11A4);
    if (!vector) return 0;
    auto* controller = vector - 0x0C;
    if (airUse.user != user || airUse.controller != controller)
        airUse = {user, controller, false};
    if (Grounded(controller)) {
        airUse.used = false;
    } else {
        if (airUse.used || nativeGetRope(controller + 0x124,
            *reinterpret_cast<unsigned*>(controller + 0x12C))) return 0;
    }
    // Keep native MP, status, pending-impact, action-lock and reuse checks.
    // Failed casts never reach Impact and do not consume the airborne use.
    return nativeCast(user, skill, level);
}

void __fastcall Impact(void* vector, void*, double vx, double vy) {
    auto* controller = static_cast<unsigned char*>(vector);
    if (!Grounded(controller) && airUse.controller == controller) {
        vx *= kAirHorizontalScale;
        airUse.used = true;
    }
    // Native Combat Step supplies vy=0. CVecCtrl::Impact leaves the current
    // vertical velocity alone for a zero vertical impulse (9B230D..9B239B).
    nativeImpact(vector, vx, vy);
}

void __cdecl Attached(void* controller, void* foothold) {
    if (foothold && airUse.controller == controller) airUse.used = false;
}

// Observe the actual foothold assignment, including landing immediately
// followed by a jump, without waiting for a later UI/update callback.
__declspec(naked) void StoreFoothold() {
    __asm {
        mov [esi+110h], edi
        pushfd
        pushad
        push edi
        push esi
        call Attached
        add esp, 8
        popad
        popfd
        jmp dword ptr [attachContinue]
    }
}

struct Patch {
    DWORD rva;
    unsigned size;
    unsigned char expected[32];
    unsigned char replacement[32];
};
bool Write(unsigned char* target, const unsigned char* bytes, unsigned size) {
    DWORD previous;
    if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &previous)) return false;
    std::memcpy(target, bytes, size);
    FlushInstructionCache(GetCurrentProcess(), target, size);
    DWORD ignored;
    return VirtualProtect(target, size, previous, &ignored) != FALSE;
}
void Branch(Patch& patch, unsigned char opcode, unsigned char* base, void* destination) {
    std::memset(patch.replacement, 0x90, patch.size);
    patch.replacement[0] = opcode;
    const DWORD relative = reinterpret_cast<DWORD>(destination) -
        reinterpret_cast<DWORD>(base + patch.rva + 5);
    std::memcpy(patch.replacement + 1, &relative, sizeof(relative));
}
}

bool AranCombatStep::Install(HMODULE module) {
    if (installed) return true;
    if (!module) return false;
    auto* base = reinterpret_cast<unsigned char*>(module);
    constexpr unsigned char skillCheck[] = {0x81,0x7d,0x08,0x29,0x73,0x40,0x01};
    constexpr unsigned char groundCheck[] = {0x39,0xb9,0x10,0x01,0x00,0x00};
    if (std::memcmp(base + 0x3607B2, skillCheck, sizeof(skillCheck)) ||
        std::memcmp(base + 0x56D9CD, groundCheck, sizeof(groundCheck))) return false;
    Patch patches[] = {
        // Retain WZ x instead of the hardcoded 650..1000 table.
        {0x3607CA, 29, {0x75,0x1b,0x8b,0x45,0x10,0x8b,0x0c,0x85,0x5c,0xb5,0xbd,0x00,
            0x8d,0x96,0x3c,0x01,0x00,0x00,0xe8,0xd0,0x5d,0xcb,0xff,0x89,0x86,0x44,0x01,0x00,0x00}, {}},
        {0x56902E, 5, {0xe8,0x2b,0x49,0x00,0x00}, {}},
        {0x56D9D3, 6, {0x0f,0x84,0x5d,0x01,0x00,0x00}, {0x90,0x90,0x90,0x90,0x90,0x90}},
        {0x56DAFA, 5, {0xe8,0x54,0x88,0xe3,0xff}, {}},
        {0x5B49C9, 6, {0x89,0xbe,0x10,0x01,0x00,0x00}, {}}
    };
    for (const auto& patch : patches)
        if (std::memcmp(base + patch.rva, patch.expected, patch.size)) return false;
    std::memcpy(patches[0].replacement, patches[0].expected, patches[0].size);
    patches[0].replacement[0] = 0xeb;
    Branch(patches[1], 0xe8, base, reinterpret_cast<void*>(&Cast));
    Branch(patches[3], 0xe8, base, reinterpret_cast<void*>(&Impact));
    Branch(patches[4], 0xe9, base, reinterpret_cast<void*>(&StoreFoothold));
    nativeCast = reinterpret_cast<CastSkill>(base + 0x56D95E);
    nativeImpact = reinterpret_cast<SetImpact>(base + 0x3A6353);
    nativeGetRope = reinterpret_cast<GetRope>(base + 0x0B1FE1);
    attachContinue = reinterpret_cast<DWORD>(base + 0x5B49CF);
    for (unsigned i = 0; i < ARRAYSIZE(patches); ++i) {
        if (!Write(base + patches[i].rva, patches[i].replacement, patches[i].size)) {
            for (unsigned j = 0; j <= i; ++j)
                Write(base + patches[j].rva, patches[j].expected, patches[j].size);
            return false;
        }
    }
    airUse = {};
    installed = true;
    return true;
}

void AranCombatStep::OnFieldDispose() { airUse = {}; }
