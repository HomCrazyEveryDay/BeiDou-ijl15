#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../ezorsia/PetPotionPersistence.cpp"

static void Require(bool ok, const char* message) {
    if (!ok) { std::printf("FAIL %s\n", message); std::exit(1); }
}
static BYTE* MapClient(const char* path) {
    FILE* file = nullptr; Require(!fopen_s(&file, path, "rb"), "open client");
    fseek(file, 0, SEEK_END); std::vector<BYTE> bytes(ftell(file)); rewind(file);
    Require(fread(bytes.data(), 1, bytes.size(), file) == bytes.size(), "read client"); fclose(file);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    auto base = static_cast<BYTE*>(VirtualAlloc(reinterpret_cast<void*>(0x30400000),
        nt->OptionalHeader.SizeOfImage, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    Require(base == reinterpret_cast<BYTE*>(0x30400000), "map EXE in isolated test; never run game entry point");
    memcpy(base, bytes.data(), nt->OptionalHeader.SizeOfHeaders);
    auto section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        memcpy(base + section[i].VirtualAddress, bytes.data() + section[i].PointerToRawData, section[i].SizeOfRawData);
    return base;
}
static void Jump(BYTE* site, void* target) {
    site[0] = 0xe9;
    const DWORD distance = reinterpret_cast<DWORD>(target) - reinterpret_cast<DWORD>(site) - 5;
    memcpy(site + 1, &distance, 4);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
}
static BYTE user[0x1ee0]{}, pets[24]{}, pet[0x180]{}, keymap[0x3c8]{};
static void* canvasMethods[36]{};
static void** canvas = canvasMethods;
static DWORD frame[80]{}, *framePointer = frame + 60;
static DWORD drawEntry = 0;
static int hpClears = 0, mpClears = 0, shades = 0;
static int& Hp() { return *reinterpret_cast<int*>(keymap + 0x3c0); }
static int& Mp() { return *reinterpret_cast<int*>(keymap + 0x3c4); }
static int __cdecl Fuse(int* value, int) { return *value; }
static void __fastcall SetHp(void* self, void*, int id) {
    Require(self == keymap, "HP keymap instance"); Hp() = id; ++hpClears;
}
static void __fastcall SetMp(void* self, void*, int id) {
    Require(self == keymap, "MP keymap instance"); Mp() = id; ++mpClears;
}
static HRESULT __stdcall Shade(void*, int x, int y, int width, int height, unsigned color) {
    Require((x == 46 || x == 112) && y == 11 && width == 32 && height == 32
        && color == 0x40ff0000, "native disabled-slot shading preserved");
    ++shades; return S_OK;
}
__declspec(naked) static void DrawDisabledSlots() {
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov ebp, framePointer
        xor ebx, ebx
        lea eax, user
        mov esi, 40ff0000h
        mov edi, 32
        call dword ptr [drawEntry]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
static void State(bool summoned, bool hpAbility = true, bool mpAbility = true) {
    *reinterpret_cast<void**>(pets + 4) = summoned ? pet : nullptr;
    *reinterpret_cast<int*>(pet + 0x164) = hpAbility;
    *reinterpret_cast<int*>(pet + 0x170) = mpAbility;
    shades = hpClears = mpClears = 0;
}
int main(int argc, char** argv) {
    Require(argc == 2, "client path argument");
    auto base = MapClient(argv[1]);
    drawEntry = reinterpret_cast<DWORD>(base + 0x4015d0);
    // This EXE has no relocation table. Relocate only the three global loads
    // reached by this isolated block; relative calls follow the mapped image.
    const DWORD keymapAddress = reinterpret_cast<DWORD>(base + 0x7ed5a0);
    const DWORD userAddress = reinterpret_cast<DWORD>(base + 0x7ebf98);
    memcpy(base + 0x401637, &keymapAddress, 4);
    memcpy(base + 0x401642, &userAddress, 4);
    memcpy(base + 0x4016a5, &keymapAddress, 4);
    *reinterpret_cast<void**>(base + 0x7ebf98) = user;
    *reinterpret_cast<void**>(base + 0x7ed5a0) = keymap;
    *reinterpret_cast<void**>(user + 0x1edc) = pets;
    canvasMethods[35] = reinterpret_cast<void*>(Shade);
    framePointer[2] = reinterpret_cast<DWORD>(&canvas);
    Jump(base + 0x108a95, Fuse);
    Jump(base + 0x18e03c, SetHp);
    Jump(base + 0x18e0a8, SetMp);
    base[0x4016af] = 0xc3; // End just this Draw block; no rendering engine or game startup.
    Hp() = 2000004; Mp() = 2000006;
    State(false); DrawDisabledSlots();
    Require(Hp() == 0 && Mp() == 0 && hpClears == 1 && mpClears == 1 && shades == 2,
        "reproduce original recall clearing both bindings while drawing disabled slots");

    Require(!PetPotionPersistence::Install(nullptr), "reject null module");
    base[0x4016aa] ^= 1;
    Require(!PetPotionPersistence::Install(reinterpret_cast<HMODULE>(base)) && base[0x40163b] == 0x53,
        "reject mismatched MP signature before touching HP");
    base[0x4016aa] ^= 1;
    const std::vector<BYTE> before(base + 0x3ff000, base + 0x401a00);
    Require(PetPotionPersistence::Install(reinterpret_cast<HMODULE>(base)), "install production patch");
    Require(PetPotionPersistence::Install(reinterpret_cast<HMODULE>(base)), "idempotent install");
    for (size_t i = 0; i < before.size(); ++i) {
        const size_t rva = 0x3ff000 + i;
        const bool patched = (rva >= 0x40163b && rva < 0x401641) || (rva >= 0x4016a9 && rva < 0x4016af);
        if (!patched) Require(base[rva] == before[i],
            "only 12 automatic-clear bytes changed; manual setters, icon draw and checks intact");
    }
    Hp() = 2000004; Mp() = 2000006;
    for (int cycle = 0; cycle < 20; ++cycle) {
        State(false); DrawDisabledSlots();
        Require(Hp() == 2000004 && Mp() == 2000006 && !hpClears && !mpClears && shades == 2,
            "recall retains both potions with disabled shading and no clear packet");
        State(true); DrawDisabledSlots();
        Require(Hp() == 2000004 && Mp() == 2000006 && !hpClears && !mpClears && !shades,
            "summon restores enabled slots with the same saved potions");
    }
    State(true, false, true); DrawDisabledSlots();
    Require(shades == 1 && Hp() == 0 && Mp() == 2000006 && hpClears == 1,
        "active pet HP capability loss retains original clear behavior");
    Hp() = 2000004;
    State(true, true, false); DrawDisabledSlots();
    Require(shades == 1 && Hp() == 2000004 && Mp() == 0 && mpClears == 1,
        "active pet MP capability loss retains original clear behavior");
    Hp() = Mp() = 0;
    State(false); DrawDisabledSlots(); State(true); DrawDisabledSlots();
    Require(Hp() == 0 && Mp() == 0, "explicitly empty settings stay empty; no stale cache restoration");
    std::puts("PASS: original bug reproduced; recall/summon, shading, capabilities, empty settings and patch bounds verified");
}
