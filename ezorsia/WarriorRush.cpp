#include "stdafx.h"
#include "WarriorRush.h"
#include <cstring>

namespace {
DWORD groundContinue, tickReturn;
unsigned char** physicalSpace;
using QueryFootholds = void(__thiscall*)(void*, int, int, int, int, void*);
using ClearFootholds = void(__thiscall*)(void*);
using CheckCorner = int(__cdecl*)(const void*, const void*, int, int);
using GetRope = void*(__cdecl*)(const void*, unsigned);
using GetDouble = double(__cdecl*)(const void*, unsigned);
using SetDouble = unsigned(__thiscall*)(void*, double);
QueryFootholds queryFootholds;
ClearFootholds clearFootholds;
CheckCorner checkCorner;
GetRope getRope;
GetDouble getDouble;
SetDouble setDouble;
void* footholdListVtable;
bool installed = false;

template<typename T> T& Field(unsigned char* object, unsigned offset) {
    return *reinterpret_cast<T*>(object + offset);
}

// ZList<ZRef<CStaticFoothold>>, as constructed by CVecCtrl's airborne
// collision routine (9B3612..9B364D). Use the matching native allocator/free.
struct Footholds {
    void* vtable = footholdListVtable;
    unsigned unused = 0, count = 0;
    unsigned char* head = nullptr;
    unsigned char* tail = nullptr;
    ~Footholds() { clearFootholds(this); }
};
static_assert(sizeof(Footholds) == 0x14, "native foothold list layout");

// Sweep the extra horizontal Rush segment BEFORE raw_Move overwrites both
// position snapshots. Native air physics can then still handle gravity and
// landing once, without a second physics update or movement-path callback.
bool Sweep(unsigned char* controller, unsigned char* space, int fromX, int y, int& x) {
    Footholds candidates;
    queryFootholds(space + 0x44, fromX, y, x, y, &candidates);
    const int targetX = x;
    const long long dx = static_cast<long long>(targetX) - fromX;
    if (!dx) return false;
    const int direction = dx > 0 ? 1 : -1;
    long long distance = dx * direction;
    bool blocked = false;
    for (auto* node = candidates.head; node;) {
        auto* foothold = Field<unsigned char*>(node, 4);
        auto* next = Field<unsigned char*>(node - 0x10, 4);
        node = next ? next + 0x10 : nullptr;
        // Same drop-through target and wall/page filtering as 9B3683..9B3712.
        auto* target = Field<unsigned char*>(controller, 0x120);
        if (target && target != foothold) continue;
        if (Field<double>(foothold, 0x30) <= 0 &&
            Field<int>(foothold, 0x20) != Field<int>(space, 0x40) &&
            Field<int>(foothold, 0x20) != Field<int>(controller, 0x134)) {
            auto* owner = Field<unsigned char*>(controller, 0x14);
            using GetType = int(__thiscall*)(void*);
            if (!owner || reinterpret_cast<GetType>(*reinterpret_cast<void***>(owner)[0])(owner) != 7)
                continue;
            auto* previous = Field<unsigned char*>(controller, 0x114);
            if (previous && Field<int>(previous, 0x1C) != Field<int>(foothold, 0x1C)) continue;
        }
        const long long x1 = Field<int>(foothold, 0x0C), y1 = Field<int>(foothold, 0x10);
        const long long x2 = Field<int>(foothold, 0x14), y2 = Field<int>(foothold, 0x18);
        const long long edgeX = x2 - x1, edgeY = y2 - y1;
        const long long before = (y - y1) * edgeX - (fromX - x1) * edgeY;
        const long long after = (y - y1) * edgeX - (targetX - x1) * edgeY;
        if (before > 0 || after < 0 || (before == 0 && after == 0)) continue;
        const long long startSide = (y1 - y) * dx, endSide = (y2 - y) * dx;
        if ((startSide < 0 && endSide < 0) || (startSide > 0 && endSide > 0)) continue;
        const bool fallingOnTarget = target == foothold &&
            getDouble(controller + 0x68, Field<unsigned>(controller, 0x78)) > 0;
        // Endpoints are not solid in isolation. Keep the native connected-edge
        // rule (9B380B..9B3867), including one-way platforms and convex corners.
        if (!fallingOnTarget) {
            if (startSide == 0) {
                auto* previous = Field<unsigned char*>(foothold, 0x4C);
                if (!previous || !checkCorner(previous, foothold, targetX, y)) continue;
            } else if (endSide == 0) {
                auto* nextFoothold = Field<unsigned char*>(foothold, 0x50);
                if (!nextFoothold || !checkCorner(foothold, nextFoothold, targetX, y)) continue;
            }
        }
        // Last integer position strictly before contact, also for leftward
        // and fractional intersections. Never teleport onto/through a slope.
        const long long numerator = dx * direction * -before;
        const long long safeDistance = numerator ? (numerator - 1) / (after - before) : 0;
        if (safeDistance < distance) distance = safeDistance;
        blocked = true;
    }
    x = static_cast<int>(fromX + direction * distance);
    return blocked;
}

void Stop(unsigned char* user, unsigned char* controller, bool stopHorizontal) {
    // Native failed-terrain path at 94E880 only clears this active flag.
    // Keep the original reuse deadline and let the next cast replace its list.
    Field<int>(user, 0x2B34) = 0;
    if (stopHorizontal) {
        Field<unsigned>(controller, 0x60) = setDouble(controller + 0x50, 0);
        Field<unsigned>(controller, 0xC0) = setDouble(controller + 0xB0, 0);
    }
}

bool IsRush(int skill) {
    return skill == 1121006 || skill == 1221007 || skill == 1321003;
}

// The native 30 ms tick already calculated the signed step and deceleration.
bool __cdecl AirTick(unsigned char* user, int x, int y, int fromX) {
    if (!IsRush(*reinterpret_cast<int*>(user + 0x2B30))) return false;
    auto* vector = *reinterpret_cast<unsigned char**>(user + 0x11A4);
    if (!vector) return false;
    auto* controller = vector - 0x0C;
    if (*reinterpret_cast<void**>(controller + 0x110)) return false;
    const unsigned ropeChecksum = Field<unsigned>(controller, 0x12C);
    if (ropeChecksum && getRope(controller + 0x124, ropeChecksum)) {
        Stop(user, controller, false);
        return true;
    }
    auto* space = *physicalSpace;
    if (!space) { Stop(user, controller, false); return true; }
    const int left = *reinterpret_cast<int*>(space + 0x24);
    const int right = *reinterpret_cast<int*>(space + 0x2C) - 1;
    if (left > right) { Stop(user, controller, false); return true; }
    bool blocked = x <= left || x >= right;
    if (x < left) x = left;
    if (x > right) x = right;
    if (Sweep(controller, space, fromX, y, x)) blocked = true;
    if (blocked) Stop(user, controller, true);
    // Avoid repeatedly clearing physical state when pressed against a wall.
    if (x == fromX) return true;

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
    } else Stop(user, controller, true);
    return true;
}

__declspec(naked) void Tick() {
    __asm {
        mov esi, [esi+8]
        add [ebp-18h], esi
        pushfd
        pushad
        mov eax, [ebp-18h]
        sub eax, esi
        push eax
        push ebx
        push dword ptr [ebp-18h]
        push edi
        call AirTick
        add esp, 10h
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
    // Guard the native services/layout anchors before installing any hook.
    const struct { DWORD rva; unsigned char bytes[6]; } services[] = {
        {0x108740, {0xb8,0xf0,0x82,0xa8,0x00,0xe8}}, // spatial query
        {0x10910F, {0x56,0x8b,0xf1,0x8b,0x46,0x0c}}, // matching list cleanup
        {0x5B3F1B, {0x55,0x8b,0xec,0x51,0x51,0x8b}}, // connected-edge collision
        {0x0B1FE1, {0x55,0x8b,0xec,0x8b,0x45,0x08}}, // secure rope getter
        {0x139338, {0x55,0x8b,0xec,0x83,0xec,0x14}}, // secure double getter
        {0x1393B6, {0x55,0x8b,0xec,0x83,0xec,0x10}}, // secure double setter
        {0x5B3712, {0x8b,0x56,0x0c,0x8b,0x7e,0x14}} // foothold coordinates
    };
    for (const auto& service : services)
        if (std::memcmp(base + service.rva, service.bytes, sizeof(service.bytes))) return false;
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
    queryFootholds = reinterpret_cast<QueryFootholds>(base + 0x108740);
    clearFootholds = reinterpret_cast<ClearFootholds>(base + 0x10910F);
    checkCorner = reinterpret_cast<CheckCorner>(base + 0x5B3F1B);
    getRope = reinterpret_cast<GetRope>(base + 0x0B1FE1);
    getDouble = reinterpret_cast<GetDouble>(base + 0x139338);
    setDouble = reinterpret_cast<SetDouble>(base + 0x1393B6);
    footholdListVtable = base + 0x6F3748;
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

#ifdef WARRIOR_RUSH_TEST
extern "C" void WarriorRushTestSetServices(void* query, void* clear) {
    queryFootholds = reinterpret_cast<QueryFootholds>(query);
    clearFootholds = reinterpret_cast<ClearFootholds>(clear);
}
#endif
