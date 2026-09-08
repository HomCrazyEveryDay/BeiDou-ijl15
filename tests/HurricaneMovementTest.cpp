#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static DWORD NativeAddress(DWORD address) {
    return address < 0x10000000 ? address + 0x30000000 : address;
}

template<typename T> static T& NativeGlobal(DWORD address) {
    return *reinterpret_cast<T*>(NativeAddress(address));
}

class Memory {
public:
    static void CodeCave(void* target, DWORD address, int size) {
        address = NativeAddress(address);
        auto* code = reinterpret_cast<unsigned char*>(address);
        memset(code, 0x90, size);
        code[0] = 0xE9;
        *reinterpret_cast<DWORD*>(code + 1) = NativeAddress(reinterpret_cast<DWORD>(target)) - address - 5;
        FlushInstructionCache(GetCurrentProcess(), code, size);
    }
};

// Generated from dllmain.cpp by the runner, so the test executes the production hooks.
#include "HurricaneMovementUnderTest.h"

static unsigned char g_user[0x3200], g_stats[0x800], g_context[0x3100], g_window[8];
static DWORD g_vtable[3];
static int g_keys[256];
static int g_action, g_horizontal, g_vertical, g_left, g_right, g_up, g_down, g_pose;
static bool g_foreground, g_canInput, g_forced, g_blocked;
static int g_cases;

static void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL case %d: %s\n", g_cases, message);
        std::exit(1);
    }
}

static void MapNativeImage(const wchar_t* path) {
    FILE* file = nullptr;
    Require(_wfopen_s(&file, path, L"rb") == 0, "open client executable");
    unsigned char headers[4096];
    Require(std::fread(headers, 1, sizeof(headers), file) == sizeof(headers), "read client headers");
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(headers);
    Require(dos->e_magic == IMAGE_DOS_SIGNATURE, "DOS signature");
    Require(dos->e_lfanew > 0 && dos->e_lfanew < sizeof(headers) - sizeof(IMAGE_NT_HEADERS32), "PE header offset");
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(headers + dos->e_lfanew);
    Require(pe->Signature == IMAGE_NT_SIGNATURE && pe->FileHeader.Machine == IMAGE_FILE_MACHINE_I386
        && pe->OptionalHeader.ImageBase == 0x00400000, "expected x86 client image");
    auto* image = static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(NativeAddress(0x00400000)),
        pe->OptionalHeader.SizeOfImage, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    Require(image == reinterpret_cast<void*>(NativeAddress(0x00400000)), "reserve test image addresses");
    std::fseek(file, 0, SEEK_END);
    std::vector<unsigned char> bytes(std::ftell(file));
    std::rewind(file);
    Require(std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size(), "read client executable");
    std::fclose(file);
    pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    const auto* section = IMAGE_FIRST_SECTION(pe);
    for (int i = 0; i < pe->FileHeader.NumberOfSections; ++i) {
        memcpy(image + section[i].VirtualAddress, bytes.data() + section[i].PointerToRawData,
            section[i].SizeOfRawData);
    }
    // The EXE has no relocation table. Rebase only the eight absolute operands
    // in the movement routine; relative calls and branches already stay valid.
    const DWORD operands[][2] = {
        {0x009CBF05, 0x00BEBF98}, {0x009CBF6D, 0x00BE7B38},
        {0x009CBF76, 0x00BF04E0}, {0x009CBF84, 0x00BEC20C},
        {0x009CBF97, 0x00BE7910}, {0x009CC001, 0x00BEC33C},
        {0x009CC030, 0x00BEC33C}, {0x009CC08D, 0x00BE7918},
    };
    for (const auto& operand : operands) {
        auto& value = NativeGlobal<DWORD>(operand[0]);
        Require(value == operand[1], "native absolute operand");
        value = NativeAddress(value);
    }
    // Only the movement routine is called. The client's entry point is never executed.
}

static int __fastcall NativeMovementBlocked(void*, void*) { return g_blocked; }
static int __fastcall NativeForcedInput(void*, void*) { return g_forced; }
static void __fastcall NativeGetForcedInput(void*, void*, int* horizontal, int* vertical) {
    *horizontal = 1;
    *vertical = -1;
}
static HWND WINAPI NativeForegroundWindow() { return reinterpret_cast<HWND>(g_foreground ? 1 : 2); }
static int __fastcall NativeCanInput(void*, void*) { return g_canInput; }
static int __fastcall NativeAction(void*, void*) { return g_action; }
static int __fastcall NativeKey(void*, void*, int key) { return g_keys[key]; }
static void* __fastcall NativeStats(void*, void*) { return g_stats; }
static int __cdecl NativeStatValue(const int* value, int) { return *value; }
static void __fastcall NativeSetPose(void*, void*, int action) { g_pose = action; }
static void __fastcall NativeSetDirections(void*, void*, int left, int right, int up, int down) {
    g_left = left;
    g_right = right;
    g_up = up;
    g_down = down;
}
static void __fastcall NativeSetInput(void*, void*, int horizontal, int vertical) {
    g_horizontal = horizontal;
    g_vertical = vertical;
}
static void __fastcall NativePhysicsUpdate(void*, void*, int) {}

static void InstallTestStubs() {
    Memory::CodeCave(NativeMovementBlocked, 0x0095F914, 5);
    Memory::CodeCave(NativeForcedInput, 0x0094BE9C, 5);
    Memory::CodeCave(NativeGetForcedInput, 0x0095CD5B, 5);
    Memory::CodeCave(NativeCanInput, 0x009E06C5, 5);
    Memory::CodeCave(NativeAction, 0x00451B6A, 5);
    Memory::CodeCave(NativeKey, 0x0059A25A, 5);
    Memory::CodeCave(NativeStatValue, 0x00416563, 5);
    Memory::CodeCave(NativeSetPose, 0x004571AB, 5);
    Memory::CodeCave(NativeSetDirections, 0x0068ADA5, 5);
    Memory::CodeCave(NativeSetInput, 0x009B7B4A, 5);
    Memory::CodeCave(NativePhysicsUpdate, 0x009B19D0, 5);
    Memory::CodeCave(reinterpret_cast<void*>(0x009CC620), 0x009CC0DF, 5);
    NativeGlobal<void*>(0x00BF04E0) = NativeForegroundWindow;
    NativeGlobal<void*>(0x00BEBF98) = g_user;
    NativeGlobal<void*>(0x00BE7B38) = g_window;
    NativeGlobal<void*>(0x00BEC20C) = g_context;
    NativeGlobal<void*>(0x00BEC33C) = g_context;
    NativeGlobal<void*>(0x00BE7918) = g_context;
    g_vtable[2] = reinterpret_cast<DWORD>(NativeStats);
    *reinterpret_cast<DWORD*>(g_window + 4) = 1;
}

static void Reset(int skill, int action, bool reverse, int left, int right) {
    memset(g_user, 0, sizeof(g_user));
    memset(g_stats, 0, sizeof(g_stats));
    memset(g_context, 0, sizeof(g_context));
    memset(g_keys, 0, sizeof(g_keys));
    *reinterpret_cast<void**>(g_user) = g_vtable;
    *reinterpret_cast<int*>(g_user + 0x2AE8) = skill;
    *reinterpret_cast<int*>(g_user + 0x574) = action;
    *reinterpret_cast<int*>(g_stats + 0x7D4) = reverse ? 1 : 0;
    NativeGlobal<int>(0x00BE7910) = 0;
    g_keys[VK_LEFT] = left;
    g_keys[VK_RIGHT] = right;
    g_action = action;
    g_foreground = g_canInput = true;
    g_forced = g_blocked = false;
    g_horizontal = g_vertical = g_left = g_right = g_up = g_down = g_pose = -99;
}

static void Run(int horizontal, int vertical, bool checkFacing) {
    ++g_cases;
    using Movement = int(__thiscall*)(void*, int);
    reinterpret_cast<Movement>(NativeAddress(0x009CBEFB))(g_context, 16);
    if (g_horizontal != horizontal) {
        std::fprintf(stderr, "skill=%d reverse=%d expected horizontal=%d actual=%d\n",
            *reinterpret_cast<int*>(g_user + 0x2AE8), *reinterpret_cast<int*>(g_stats + 0x7D4),
            horizontal, g_horizontal);
    }
    Require(g_horizontal == horizontal, "horizontal movement");
    Require(g_vertical == vertical, "vertical movement");
    Require(g_left == (horizontal < 0) && g_right == (horizontal > 0), "horizontal direction flags");
    Require(g_up == (vertical < 0) && g_down == (vertical > 0), "vertical direction flags");
    if (checkFacing) {
        Require((*reinterpret_cast<int*>(g_user + 0x570) & 1) == (horizontal < 0), "shooting facing");
        const int skill = *reinterpret_cast<int*>(g_user + 0x2AE8);
        Require(g_pose == (horizontal < 0 ? (skill == 5221004 ? 0x63 : g_action) : -99), "held shooting pose");
    }
}

int wmain(int argc, wchar_t** argv) {
    Require(argc == 2, "client executable argument");
    MapNativeImage(argv[1]);
    InstallTestStubs();
    InstallHurricaneMovement();
    for (int skill : {3121004, 5221004}) {
        const int action = skill == 3121004 ? 0x60 : 0x63;
        for (bool reverse : {true, false}) {
            for (int left : {0, 1}) {
                for (int right : {0, 1}) {
                    Reset(skill, action, reverse, left, right);
                    // Holding up/down must not add ladder movement to held shooting.
                    g_keys[VK_UP] = 1;
                    Run((right - left) * (reverse ? -1 : 1), 0, true);
                }
            }
        }
        Reset(skill, action, true, 1, 0);
        Run(1, 0, true);
        *reinterpret_cast<int*>(g_stats + 0x7D4) = 0;
        Run(-1, 0, true);
        for (int lock : {0, 1, 2, 3}) {
            Reset(skill, action, true, 1, 0);
            if (lock == 0) g_foreground = false;
            if (lock == 1) g_canInput = false;
            if (lock == 2) NativeGlobal<int>(0x00BE7910) = 1;
            if (lock == 3) *reinterpret_cast<int*>(g_context + 0x30B0) = 1;
            Run(0, 0, true);
        }
        Reset(skill, action, true, 1, 0);
        g_forced = true;
        Run(1, -1, true);
    }
    for (bool reverse : {false, true}) {
        Reset(0, -1, reverse, 1, 0);
        g_keys[VK_UP] = 1;
        Run(reverse ? 1 : -1, reverse ? 1 : -1, false);
        Reset(0, 0x60, reverse, 1, 0);
        Run(0, 0, false);
    }
    std::printf("PASS HurricaneMovementTest: %d native movement cases\n", g_cases);
    return 0;
}
