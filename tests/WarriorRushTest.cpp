#include "../ezorsia/WarriorRush.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" void WarriorRushTestSetServices(void* query, void* clear);

static void Require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static unsigned char* activeUser;
static unsigned char* activeController;
static int terrainQueries;
static bool slopeFixture;
static unsigned char slopeNode[0x30]{};
static unsigned char slopeFoothold[0x60]{};
static POINT point;
static int __cdecl Now() { return 10000; }
static POINT* __fastcall Position(void*, void*) {
    point.x = static_cast<LONG>(*reinterpret_cast<double*>(activeController + 0x20));
    point.y = static_cast<LONG>(*reinterpret_cast<double*>(activeController + 0x38));
    return &point;
}
static unsigned __fastcall StoreDouble(double* field, void*, double value) { *field = value; return 0x12345678; }
static unsigned __fastcall StoreRope(void* value, void** field) { *field = value; return 0; }
static void __fastcall Remove(void*, void*, void* elem) {
    --*reinterpret_cast<int*>(activeUser + 0x2B40);
    *reinterpret_cast<void**>(activeUser + 0x2B44) = static_cast<int*>(elem) + 3;
}
static void* __fastcall Below(void*, void*, int, int, int* y, int, int) {
    ++terrainQueries; *y = 101; return reinterpret_cast<void*>(1);
}
static void* __fastcall Above(void*, void*, int, int, int*, int) { ++terrainQueries; return nullptr; }
static void __stdcall TestQueryImpl(int, int, int, int, void* list) {
    if (!slopeFixture) return;
    *reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(list) + 0x0C) = slopeNode + 0x10;
    *reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(list) + 0x10) = slopeNode + 0x10;
    *reinterpret_cast<int*>(static_cast<unsigned char*>(list) + 0x08) = 1;
    *reinterpret_cast<unsigned char**>(slopeNode + 0x14) = slopeFoothold;
    *reinterpret_cast<unsigned char**>(slopeNode + 0x04) = nullptr;
}
__declspec(naked) static void TestQuery() {
    __asm {
        push dword ptr [esp+14h]
        push dword ptr [esp+14h]
        push dword ptr [esp+14h]
        push dword ptr [esp+14h]
        push dword ptr [esp+14h]
        call TestQueryImpl
        ret 14h
    }
}
__declspec(naked) static void TestClear() { __asm { ret } }
__declspec(naked) void Round() {
    __asm {
        sub esp, 12
        fnstcw word ptr [esp+8]
        mov ax, [esp+8]
        or ax, 0C00h
        mov [esp+10], ax
        fldcw word ptr [esp+10]
        fistp qword ptr [esp]
        fldcw word ptr [esp+8]
        mov eax, [esp]
        mov edx, [esp+4]
        add esp, 12
        ret
    }
}
static int __cdecl Abs(int value) { return value < 0 ? -value : value; }
static void Jump(unsigned char* site, void* dest) {
    site[0] = 0xe9;
    *reinterpret_cast<DWORD*>(site + 1) = reinterpret_cast<DWORD>(dest) - reinterpret_cast<DWORD>(site + 5);
}
__declspec(naked) int Gate(DWORD address, int skill, void* controller) {
    __asm {
        push edi
        mov eax, [esp+0Ch]
        mov edi, [esp+10h]
        call dword ptr [esp+8]
        pop edi
        ret
    }
}
static void Return(unsigned char* site, int value) {
    site[0] = 0xb8; *reinterpret_cast<int*>(site + 1) = value; site[5] = 0xc3;
}

__declspec(naked) void SetDeadline(DWORD address, void* user, int now) {
    __asm {
        push ebx
        push edi
        mov ebx, [esp+10h]
        mov edi, [esp+14h]
        call dword ptr [esp+0Ch]
        pop edi
        pop ebx
        ret
    }
}
__declspec(naked) int ReuseGate(DWORD address, void* user, int skill, int now) {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 60h
        push ebx
        mov ebx, [ebp+0Ch]
        mov eax, [ebp+10h]
        mov ecx, [ebp+14h]
        mov [ebp-5Ch], ecx
        mov ecx, 04DD5CCh
        mov edx, 0E66C4Bh
        call dword ptr [ebp+8]
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    }
}

int main(int argc, char** argv) {
    Require(argc == 2, "EXE path");
    FILE* file = nullptr;
    Require(fopen_s(&file, argv[1], "rb") == 0, "open image");
    fseek(file, 0, SEEK_END); std::vector<unsigned char> bytes(ftell(file)); rewind(file);
    Require(fread(bytes.data(), 1, bytes.size(), file) == bytes.size(), "read image"); fclose(file);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    Require(nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.ImageBase == 0x400000, "v83 PE");
    auto* mapped = static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(0x10400000),
        nt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    Require(mapped != nullptr, "map isolated image");
    auto* sections = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        std::memcpy(mapped + sections[i].VirtualAddress, bytes.data() + sections[i].PointerToRawData, sections[i].SizeOfRawData);
    // Run the original deadline assignment and the actual melee reuse gate.
    // Stop after the native store; remove its pending push 9 before returning.
    mapped[0x552F0D] = 0x59; mapped[0x552F0E] = 0xc3;
    Return(mapped + 0x550AE8, 0); Return(mapped + 0x550D38, 1);
    alignas(8) unsigned char user[0x2B60]{}, controller[0x200]{}, space[0x100]{}, spatialIndex[0x40]{}, spatialRoot[0x100]{};
    const DWORD deadlineCode = reinterpret_cast<DWORD>(mapped + 0x552EF9);
    const DWORD reuseCode = reinterpret_cast<DWORD>(mapped + 0x550D05);
    auto& deadline = *reinterpret_cast<int*>(user + 0x2B4C);
    FlushInstructionCache(GetCurrentProcess(), mapped, nt->OptionalHeader.SizeOfImage);
    SetDeadline(deadlineCode, user, 10000);
    Require(deadline == 11770, "reproduce native 1770 ms lock");
    for (int skill : {1121006, 1221007, 1321003}) {
        Require(ReuseGate(reuseCode, user, skill, 10270) == 0, "native lock remains after movement");
        Require(ReuseGate(reuseCode, user, skill, 11000) == 0, "native lock remains after one second");
        Require(ReuseGate(reuseCode, user, skill, 11770) == 1, "native lock expires at 1770 ms");
    }
    Require(!WarriorRush::Install(nullptr), "null guard");
    for (DWORD address : {0x550B54u, 0x550B59u, 0x54E764u, 0x552EFBu, 0x552F09u}) {
        mapped[address] ^= 1;
        auto saved = std::vector<unsigned char>(mapped, mapped + nt->OptionalHeader.SizeOfImage);
        Require(!WarriorRush::Install(reinterpret_cast<HMODULE>(mapped)), "signature guard");
        Require(std::memcmp(saved.data(), mapped, saved.size()) == 0, "failure makes no writes");
        mapped[address] ^= 1;
    }
    auto before = std::vector<unsigned char>(mapped, mapped + nt->OptionalHeader.SizeOfImage);
    Require(WarriorRush::Install(reinterpret_cast<HMODULE>(mapped)), "install");
    Require(WarriorRush::Install(reinterpret_cast<HMODULE>(mapped)), "idempotent install");
    WarriorRushTestSetServices(reinterpret_cast<void*>(&TestQuery), reinterpret_cast<void*>(&TestClear));
    for (size_t i = 0; i < before.size(); ++i) if (before[i] != mapped[i])
        Require(i == 0x550B5A || i == 0x550B65 || i == 0x550B70 || (i >= 0x54E762 && i < 0x54E768) ||
            i == 0x552EFB || i == 0x552EFC, "patch scope");

    SetDeadline(deadlineCode, user, 10000);
    Require(deadline == 10270, "only 270 ms native movement window remains");
    for (int skill : {1121006, 1221007, 1321003}) {
        for (int elapsed : {0, 90, 180, 269})
            Require(ReuseGate(reuseCode, user, skill, 10000 + elapsed) == 0, "cannot restart during movement");
        for (int elapsed : {270, 300, 600, 1000})
            Require(ReuseGate(reuseCode, user, skill, 10000 + elapsed) == 1, "no hidden wait after movement");
    }
    using Delay = int(__cdecl*)(int);
    const auto reuseDelay = reinterpret_cast<Delay>(mapped + 0x5535E3);
    for (int skill : {1121006, 1221007, 1321003})
        Require(reuseDelay(skill) == 0, "Rush has no second skill reuse timer");
    Require(reuseDelay(21001001) == 1000, "Combat Step reuse timer unchanged");
    deadline = 11770;
    Require(ReuseGate(reuseCode, user, 5101002, 11000) == 0 &&
        ReuseGate(reuseCode, user, 5101002, 11770) == 1, "other movement skill deadlines unaffected");
    std::puts("PASS: reproduced 1770 ms Rush lock; all three skills reusable at 270 ms; movement window and other skill timers preserved");

    Return(mapped + 0x550C53, 1); Return(mapped + 0x550AE8, 0);
    activeUser = user; activeController = controller;
    auto& foothold = *reinterpret_cast<void**>(controller + 0x110);
    for (int skill : {1121006, 1221007, 1321003}) {
        foothold = nullptr;
        Require(Gate(reinterpret_cast<DWORD>(mapped + 0x550B53), skill, controller) == 1, "three Rush skills pass airborne gate");
    }
    foothold = nullptr;
    Require(Gate(reinterpret_cast<DWORD>(mapped + 0x550B53), 5121004, controller) == 0, "unrelated ground skill still blocked");
    foothold = reinterpret_cast<void*>(1);
    Require(Gate(reinterpret_cast<DWORD>(mapped + 0x550B53), 5121004, controller) == 1, "unrelated ground skill still permitted on ground");

    // Execute the complete native rush tick and raw_Move. Only resource,
    // list, terrain-query and secure-field services are substituted.
    Jump(mapped + 0x587257, reinterpret_cast<void*>(&Now));
    Jump(mapped + 0x561929, reinterpret_cast<void*>(&Remove));
    Jump(mapped + 0x645585, reinterpret_cast<void*>(&Below));
    Jump(mapped + 0x64549D, reinterpret_cast<void*>(&Above));
    Jump(mapped + 0x1393B6, reinterpret_cast<void*>(&StoreDouble));
    Jump(mapped + 0x5B4B79, reinterpret_cast<void*>(&StoreRope));
    Jump(mapped + 0x662018, reinterpret_cast<void*>(&Round));
    Jump(mapped + 0x661A33, reinterpret_cast<void*>(&Abs));
    *reinterpret_cast<DWORD*>(mapped + 0x54E73D) += 0x10000000;
    *reinterpret_cast<DWORD*>(mapped + 0x54E756) += 0x10000000;
    *reinterpret_cast<void**>(mapped + 0x7EBFA0) = space;
    *reinterpret_cast<int*>(space + 0x24) = -1000;
    *reinterpret_cast<int*>(space + 0x2C) = 1000;
    // Native AirCollision queries the map's foothold index at space+44. Keep
    // an empty, valid tree for the movement tests that cover the no-foothold
    // path; collision fixtures below can populate its leaf entries.
    *reinterpret_cast<void**>(space + 0x60) = spatialRoot;
    void* userVtable[5]{}; userVtable[4] = reinterpret_cast<void*>(&Position);
    void* vectorVtable[17]{}; vectorVtable[16] = mapped + 0x5B5E7F;
    *reinterpret_cast<void**>(user + 4) = userVtable;
    *reinterpret_cast<void**>(user + 0x11A4) = controller + 0x0C;
    *reinterpret_cast<void**>(controller + 0x0C) = vectorVtable;
    FlushInstructionCache(GetCurrentProcess(), mapped, nt->OptionalHeader.SizeOfImage);
    auto tick = reinterpret_cast<void(__thiscall*)(void*)>(mapped + 0x54E6D3);
    auto& x = *reinterpret_cast<double*>(controller + 0x20);
    auto& y = *reinterpret_cast<double*>(controller + 0x38);
    auto& vy = *reinterpret_cast<double*>(controller + 0x68);
    auto& previousVy = *reinterpret_cast<double*>(controller + 0xC8);
    int elems[2][3]{};
    const auto reset = [&](int skill, int step, bool ground) {
        elems[0][0] = 0; elems[0][1] = 90; elems[0][2] = step;
        elems[1][0] = 2; elems[1][1] = 180; elems[1][2] = step;
        *reinterpret_cast<int*>(user + 0x2B30) = skill;
        *reinterpret_cast<int*>(user + 0x2B34) = 1;
        *reinterpret_cast<int*>(user + 0x2B40) = 2;
        *reinterpret_cast<void**>(user + 0x2B44) = elems;
        foothold = ground ? reinterpret_cast<void*>(1) : nullptr;
        terrainQueries = 0; x = 0; y = 100;
        *reinterpret_cast<int*>(user + 0x116C) = 0;
        *reinterpret_cast<int*>(user + 0x1170) = 100;
    };
    for (int skill : {1121006, 1221007, 1321003}) for (int direction : {-1, 1}) {
        reset(skill, direction * 46, false); vy = 350; previousVy = 300;
        int expected = 0, step = direction * 46;
        for (int i = 0; i < 9; ++i) {
            if (i >= 3) step = static_cast<int>(step * 0.8);
            expected += step;
            tick(user);
            Require(x == expected && y == 100, "native signed step and deceleration preserved in air");
            Require(vy == 350 && previousVy == 300, "raw_Move no longer resets falling velocity");
            Require(*reinterpret_cast<int*>(user + 0x116C) == expected, "movement position synchronized");
        }
        tick(user);
        Require(*reinterpret_cast<int*>(user + 0x2B40) == 0, "native rush duration terminates");
        Require(terrainQueries == 0, "air rush does not snap to platforms");
    }
    for (double vertical : {-250.0, 0.0, 300.0}) {
        reset(1121006, 46, false); vy = vertical; previousVy = vertical;
        tick(user); Require(vy == vertical && previousVy == vertical, "ascent/apex/descent preserved");
    }
    reset(1121006, 46, false); x = 990; tick(user); Require(x == 999, "right map edge");
    reset(1321003, -46, false); x = -990; tick(user); Require(x == -1000, "left map edge");
    reset(1221007, 46, true); tick(user);
    Require(terrainQueries == 2 && x == 46 && y == 100, "ground rush retains native terrain following");
    reset(5121004, 46, false); tick(user); Require(terrainQueries == 2, "other moving attacks retain native path");
    // A descending foothold crosses the horizontal Rush segment at x=45.
    // The native query list is supplied by a fixture, while the production
    // sweep still runs the real one-way/corner geometry and then stops Rush.
    std::memset(slopeNode, 0, sizeof(slopeNode));
    std::memset(slopeFoothold, 0, sizeof(slopeFoothold));
    *reinterpret_cast<int*>(slopeFoothold + 0x0C) = -10;
    *reinterpret_cast<int*>(slopeFoothold + 0x10) = 110;
    *reinterpret_cast<int*>(slopeFoothold + 0x14) = 100;
    *reinterpret_cast<int*>(slopeFoothold + 0x18) = 90;
    *reinterpret_cast<double*>(slopeFoothold + 0x30) = 1.0;
    *reinterpret_cast<int*>(slopeFoothold + 0x20) = 0;
    slopeFixture = true;
    reset(1121006, 46, false); tick(user);
    Require(x < 46 && x > 0, "air Rush stops before a descending slope");
    Require(*reinterpret_cast<int*>(user + 0x2B34) == 0, "slope collision ends current Rush");
    slopeFixture = false;
    std::puts("PASS: Rush signature/skill scope; all three airborne gates; native tick/duration/deceleration; native raw_Move vertical preservation; both directions; map edges; ground/unrelated terrain path; descending-slope collision stop");
    VirtualFree(mapped, 0, MEM_RELEASE);
}
