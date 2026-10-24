#pragma once
#include <windows.h>
#include <climits>
#include <cstring>

namespace MountRopeAlignment {
namespace Detail {
using FindRope = void* (__thiscall*)(void*, int, int, int, int);
static auto findRope = reinterpret_cast<FindRope>(0x00A45B03);

// CAvatar::PrepareActionLayer (455829) and frame update (45297B) compute
// mount.navel - body.navel. 4529A9 mirrors X, then stores the rendered offset
// at CAvatar+4D4/+4D8. The six native rope queries use unadjusted physics X.
// Read the actual controller's owner, including a private shop preview, rather
// than a global player or a table of mount IDs/resource-specific corrections.
static int BodyOffsetX(unsigned char* controller) {
    if (!controller) return 0;
    auto owner = *reinterpret_cast<unsigned char**>(controller + 0x14);
    if (!owner) return 0;
    auto user = owner - 4; // IVecCtrlOwner subobject; native 9CBDC8..9CBDCF.
    if (*reinterpret_cast<void**>(user + 0x11A4) != controller + 0x0C) return 0;
    auto avatar = user + 0x88;
    const int category = *reinterpret_cast<int*>(avatar + 0x4BC) / 10000;
    if (category != 190 && category != 193) return 0;
    if (!*reinterpret_cast<void**>(avatar + 0x10B8)) return 0; // body origin not ready
    return *reinterpret_cast<int*>(avatar + 0x4D4);
}

static void* __fastcall Query(void* space, unsigned char* controller,
    int x1, int y1, int x2, int y2) {
    const int offset = BodyOffsetX(controller);
    const long long first = static_cast<long long>(x1) + offset;
    const long long second = static_cast<long long>(x2) + offset;
    // Try the visible body first, but never remove an original native catch.
    // Shifting the only query made riders miss ropes near walls (e.g. the
    // Korean Folk Town well) even when the physics position was on the rope.
    // Keep two separate native queries: each retains +/-10 and the original
    // Y/foothold rules, without catching ropes between widely separated anchors.
    if (offset && first >= INT_MIN + 10LL && first <= INT_MAX - 10LL &&
        second >= INT_MIN + 10LL && second <= INT_MAX - 10LL) {
        if (auto rope = findRope(space, static_cast<int>(first), y1, static_cast<int>(second), y2))
            return rope;
    }
    return findRope(space, x1, y1, x2, y2);
}

// These replace CALL instructions: the native return address and four stack
// arguments stay in place. Query consumes the same 16 bytes as the original.
__declspec(naked) static void LocalQuery() {
    __asm {
        mov edx, edi
        jmp Query
    }
}
__declspec(naked) static void PreviewQuery() {
    __asm {
        mov edx, ebx
        jmp Query
    }
}

struct Site { DWORD address; void* entry; };
static const Site sites[] = {
    {0x009CC26A, LocalQuery}, {0x009CC3CA, LocalQuery}, {0x009CC550, LocalQuery},
    {0x009CF9F4, PreviewQuery}, {0x009CFB54, PreviewQuery}, {0x009CFCDA, PreviewQuery}
};

static bool WriteCall(DWORD address, const unsigned char* code) {
    auto target = reinterpret_cast<void*>(address);
    DWORD previous = 0;
    if (!VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &previous)) return false;
    std::memcpy(target, code, 5);
    FlushInstructionCache(GetCurrentProcess(), target, 5);
    DWORD ignored = 0;
    return VirtualProtect(target, 5, previous, &ignored) != FALSE;
}
}

static bool Install() {
    using namespace Detail;
    static bool installed = false;
    if (installed) return true;
    // Validate the reader ABI as well as every call before changing any code.
    const unsigned char native[] = {0x55,0x8B,0xEC,0x51,0x8B,0x55,0x08,0x8B,0x45,0x10};
    const unsigned char anchor[] = {0x8B,0x78,0x28,0x8B,0x70,0x2C,0x2B,0x79,0x1C,0x2B,0x71,0x20};
    const unsigned char offsetStore[] = {0x89,0xBB,0xD4,0x04,0x00,0x00};
    if (std::memcmp(reinterpret_cast<void*>(0x00A45B03), native, sizeof(native)) ||
        std::memcmp(reinterpret_cast<void*>(0x0045297B), anchor, sizeof(anchor)) ||
        std::memcmp(reinterpret_cast<void*>(0x004529BA), offsetStore, sizeof(offsetStore))) return false;
    unsigned char original[ARRAYSIZE(sites)][5]{};
    for (unsigned i = 0; i < ARRAYSIZE(sites); ++i) {
        std::memcpy(original[i], reinterpret_cast<void*>(sites[i].address), 5);
        int relative = 0;
        std::memcpy(&relative, original[i] + 1, 4);
        if (original[i][0] != 0xE8 || sites[i].address + 5 + relative != 0x00A45B03) return false;
    }
    for (unsigned i = 0; i < ARRAYSIZE(sites); ++i) {
        unsigned char call[5] = {0xE8};
        const DWORD relative = reinterpret_cast<DWORD>(sites[i].entry) - sites[i].address - 5;
        std::memcpy(call + 1, &relative, 4);
        if (!WriteCall(sites[i].address, call)) {
            for (unsigned j = 0; j <= i; ++j) WriteCall(sites[j].address, original[j]);
            return false;
        }
    }
    installed = true;
    return true;
}
}
