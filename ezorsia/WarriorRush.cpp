#include "stdafx.h"
#include "WarriorRush.h"
#include <cstring>

namespace {
DWORD groundContinue, tickReturn;
unsigned char** physicalSpace;
bool installed = false;

bool IsRush(int skill) {
    return skill == 1121006 || skill == 1221007 || skill == 1321003;
}

// The native 30 ms tick already calculated the signed step and deceleration.
bool __cdecl AirTick(unsigned char* user, int x, int y) {
    if (!IsRush(*reinterpret_cast<int*>(user + 0x2B30))) return false;
    auto* vector = *reinterpret_cast<unsigned char**>(user + 0x11A4);
    if (!vector) return false;
    auto* controller = vector - 0x0C;
    if (*reinterpret_cast<void**>(controller + 0x110)) return false;
    auto* space = *physicalSpace;
    if (!space) return true;
    const int left = *reinterpret_cast<int*>(space + 0x24);
    const int right = *reinterpret_cast<int*>(space + 0x2C) - 1;
    if (left > right) return true;
    if (x < left) x = left;
    if (x > right) x = right;

    // Native raw_Move clears both velocity snapshots. Restore their secure
    // vertical values at the same addresses so a rush never cancels falling.
    unsigned char vy[20], previousVy[20];
    std::memcpy(vy, controller + 0x68, sizeof(vy));
    std::memcpy(previousVy, controller + 0xC8, sizeof(previousVy));
    using Move = HRESULT(__stdcall*)(void*, int, int);
    auto move = reinterpret_cast<Move>((*reinterpret_cast<void***>(vector))[0x40 / 4]);
    const HRESULT result = move(vector, x, y);
    std::memcpy(controller + 0x68, vy, sizeof(vy));
    std::memcpy(controller + 0xC8, previousVy, sizeof(previousVy));
    if (SUCCEEDED(result)) {
        *reinterpret_cast<int*>(user + 0x116C) = x;
        *reinterpret_cast<int*>(user + 0x1170) = y;
    }
    return true;
}

__declspec(naked) void Tick() {
    __asm {
        mov esi, [esi+8]
        add [ebp-18h], esi
        pushfd
        pushad
        push ebx
        push dword ptr [ebp-18h]
        push edi
        call AirTick
        add esp, 0Ch
        test al, al
        jnz handled
        popad
        popfd
        jmp dword ptr [groundContinue]
    handled:
        popad
        popfd
        jmp dword ptr [tickReturn]
    }
}

struct Patch {
    DWORD rva;
    unsigned size;
    unsigned char expected[6];
    unsigned char replacement[6];
};
bool Write(unsigned char* target, const unsigned char* bytes, unsigned size) {
    DWORD previous;
    if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &previous)) return false;
    std::memcpy(target, bytes, size);
    FlushInstructionCache(GetCurrentProcess(), target, size);
    DWORD ignored;
    return VirtualProtect(target, size, previous, &ignored) != FALSE;
}
}

bool WarriorRush::Install(HMODULE module) {
    if (installed) return true;
    if (!module) return false;
    auto* base = reinterpret_cast<unsigned char*>(module);
    Patch patches[] = {
        // Only these three skill-specific JEs skip the shared ground check.
        // Leave the existing Boomerang Step hook at 950C46 untouched.
        {0x550B58, 6, {0x0f,0x84,0xe8,0x00,0x00,0x00}, {0x0f,0x84,0xf5,0x00,0x00,0x00}},
        {0x550B63, 6, {0x0f,0x84,0xdd,0x00,0x00,0x00}, {0x0f,0x84,0xea,0x00,0x00,0x00}},
        {0x550B6E, 6, {0x0f,0x84,0xd2,0x00,0x00,0x00}, {0x0f,0x84,0xdf,0x00,0x00,0x00}},
        {0x54E762, 6, {0x8b,0x76,0x08,0x01,0x75,0xe8}, {0xe9,0,0,0,0,0x90}},
        // Native Rush uses 90 + 180 ms of movement, then adds 1500 ms of
        // hidden reuse lock. Keep the movement window, remove only that tail.
        // This deadline assignment is inside the three-warrior-Rush branch.
        {0x552EF9, 6, {0x8d,0x87,0xea,0x06,0x00,0x00}, {0x8d,0x87,0x0e,0x01,0x00,0x00}}
    };
    constexpr unsigned char deadlineStore[] = {
        0x6a,0x09,0x8d,0x8b,0xa4,0x11,0x00,0x00,0x89,0x83,0x4c,0x2b,0x00,0x00
    };
    if (std::memcmp(base + 0x552EFF, deadlineStore, sizeof(deadlineStore))) return false;
    constexpr int skills[] = {1121006, 1221007, 1321003};
    for (unsigned i = 0; i < 3; ++i) {
        if (base[patches[i].rva - 5] != 0x3d ||
            *reinterpret_cast<int*>(base + patches[i].rva - 4) != skills[i]) return false;
    }
    for (const auto& patch : patches)
        if (std::memcmp(base + patch.rva, patch.expected, patch.size)) return false;
    const DWORD relative = reinterpret_cast<DWORD>(&Tick) - reinterpret_cast<DWORD>(base + 0x54E767);
    std::memcpy(patches[3].replacement + 1, &relative, sizeof(relative));
    groundContinue = reinterpret_cast<DWORD>(base + 0x54E768);
    tickReturn = reinterpret_cast<DWORD>(base + 0x54E887);
    physicalSpace = reinterpret_cast<unsigned char**>(base + 0x7EBFA0);
    for (unsigned i = 0; i < ARRAYSIZE(patches); ++i) {
        if (!Write(base + patches[i].rva, patches[i].replacement, patches[i].size)) {
            for (unsigned j = 0; j <= i; ++j)
                Write(base + patches[j].rva, patches[j].expected, patches[j].size);
            return false;
        }
    }
    installed = true;
    return true;
}
