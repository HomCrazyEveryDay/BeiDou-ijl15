#include "../ezorsia/AranCombatStep.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <cmath>

static void Require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static int setterCalls;
static unsigned __fastcall StoreX(int value, int* destination) {
    ++setterCalls;
    *destination = value;
    return 0x12345678;
}

static bool enoughMp = true;
static int validationCalls, reusableAt, now = 10000;
static int __fastcall True(void*, void*) { return 1; }
static int __fastcall False(void*, void*) { return 0; }
static POINT position{100, 100};
static POINT* __fastcall Position(void*, void*) { return &position; }
static void* __fastcall FindRope(void*, void*, int, int) { return nullptr; }
static int __cdecl Now() { return now; }
static int __fastcall Cooldown(void*, void*, const void*, int* end) { *end = reusableAt; return reusableAt != 0; }
static void __fastcall SaveCooldown(void*, void*, const void*, const int* end) { reusableAt = *end; }
static int __fastcall Validate(void*, void*, const int*, int, int, int, int, int, int) {
    ++validationCalls; return enoughMp;
}
static int levelDataForCast[0x200 / 4]{};
static void* __fastcall SkillLevel(void*, void*, int) { return levelDataForCast; }
static int __cdecl ReadInt(const int* value, unsigned) { return *value; }
static double __cdecl ReadDouble(const double* value, unsigned) { return *value; }
static unsigned __fastcall StoreDouble(double* value, void*, double next) { *value = next; return 0; }
static void* __cdecl ReadRope(void* const* value, unsigned) { return *value; }
static void __fastcall Attribute(void*, void*, int) {}
static void __fastcall Effect(void*, void*, const void*, int, int, int, int) {}
static int __cdecl ReuseDelay(int) { return 1000; }
__declspec(naked) void RoundVelocity() {
    __asm {
        sub esp, 8
        fistp qword ptr [esp]
        mov eax, [esp]
        mov edx, [esp+4]
        add esp, 8
        ret
    }
}
static void Jump(unsigned char* site, void* destination) {
    site[0] = 0xe9;
    const DWORD relative = reinterpret_cast<DWORD>(destination) - reinterpret_cast<DWORD>(site + 5);
    std::memcpy(site + 1, &relative, 4);
}
__declspec(naked) void Attach(DWORD address, void* controller, void* foothold) {
    __asm {
        push ebp
        mov ebp, esp
        push esi
        push edi
        mov esi, [ebp+0Ch]
        mov edi, [ebp+10h]
        call dword ptr [ebp+8]
        pop edi
        pop esi
        pop ebp
        ret
    }
}

static void AirCases(unsigned char* mapped) {
    // Execute the complete original skill function, not a rewritten copy.
    // Stub external UI/resource/MP services; its ground/pending/reuse branches,
    // facing conversion and native SetImpactNext instructions remain real.
    Jump(mapped + 0x558143, reinterpret_cast<void*>(&True));
    Jump(mapped + 0x55F914, reinterpret_cast<void*>(&False));
    Jump(mapped + 0x54BE9C, reinterpret_cast<void*>(&False));
    Jump(mapped + 0x312B61, reinterpret_cast<void*>(&FindRope));
    Jump(mapped + 0x587257, reinterpret_cast<void*>(&Now));
    Jump(mapped + 0x0E86A0, reinterpret_cast<void*>(&Cooldown));
    Jump(mapped + 0x0E86F0, reinterpret_cast<void*>(&SaveCooldown));
    Jump(mapped + 0x56D399, reinterpret_cast<void*>(&Validate));
    Jump(mapped + 0x360F23, reinterpret_cast<void*>(&SkillLevel));
    Jump(mapped + 0x016563, reinterpret_cast<void*>(&ReadInt));
    Jump(mapped + 0x0B1FE1, reinterpret_cast<void*>(&ReadRope));
    Jump(mapped + 0x12EF17, reinterpret_cast<void*>(&Attribute));
    Jump(mapped + 0x533990, reinterpret_cast<void*>(&Effect));
    Jump(mapped + 0x5535E3, reinterpret_cast<void*>(&ReuseDelay));
    Jump(mapped + 0x139338, reinterpret_cast<void*>(&ReadDouble));
    Jump(mapped + 0x1393B6, reinterpret_cast<void*>(&StoreDouble));
    Jump(mapped + 0x662018, reinterpret_cast<void*>(&RoundVelocity));
    *reinterpret_cast<DWORD*>(mapped + 0x56D97F) += 0x10000000;
    *reinterpret_cast<DWORD*>(mapped + 0x56D9DE) += 0x10000000;
    for (auto range : {std::pair<DWORD, DWORD>{0x3A6353, 0x3A645A}, {0x5B222A, 0x5B23F2}})
        for (DWORD i = range.first; i + 4 <= range.second; ++i)
            if (*reinterpret_cast<DWORD*>(mapped + i) == 0xAF0DE8)
                *reinterpret_cast<DWORD*>(mapped + i) += 0x10000000;
    mapped[0x5B49CF] = 0xc3; // stop after the actual native foothold store
    FlushInstructionCache(GetCurrentProcess(), mapped, 0x700000);

    alignas(8) unsigned char user[0x3200]{}, controller[0x200]{}, other[0x200]{};
    void* vtable[5]{}; vtable[4] = reinterpret_cast<void*>(&Position);
    *reinterpret_cast<void**>(user + 4) = vtable;
    *reinterpret_cast<void**>(user + 0x11A4) = controller + 0x0C;
    auto& foothold = *reinterpret_cast<void**>(controller + 0x110);
    auto& rope = *reinterpret_cast<void**>(controller + 0x124);
    auto& pending = *reinterpret_cast<int*>(controller + 0x188);
    auto& vx = *reinterpret_cast<double*>(controller + 0x50);
    auto& vy = *reinterpret_cast<double*>(controller + 0x68);
    const int skill = 21001001;
    const int impulse = 1155;
    levelDataForCast[0x13C / 4] = impulse;
    using Cast = int(__thiscall*)(void*, const int*, int);
    auto callSite = mapped + 0x56902E;
    const auto cast = reinterpret_cast<Cast>(callSite + 5 + *reinterpret_cast<int*>(callSite + 1));
    const auto processImpact = reinterpret_cast<void(__thiscall*)(void*)>(mapped + 0x5B222A);
    const DWORD attach = reinterpret_cast<DWORD>(mapped + 0x5B49C9);
    const auto resetAir = [&] {
        Attach(attach, controller, reinterpret_cast<void*>(1));
        Require(foothold == reinterpret_cast<void*>(1), "landing store executed");
        Attach(attach, controller, nullptr);
        pending = 0; reusableAt = 0; enoughMp = true;
    };
    for (int facing : {0, 1}) for (double vertical : {-250.0, 0.0, 300.0}) {
        resetAir(); vx = 0; vy = vertical;
        *reinterpret_cast<int*>(user + 0x570) = facing;
        Require(cast(user, &skill, 15) == 1, "air cast succeeds through native checks");
        Require(pending == 1, "native impact queued");
        Require(*reinterpret_cast<double*>(controller + 0x190) == (facing ? -impulse : impulse) * 0.408248290463863, "air horizontal impulse scaled independently");
        Require(*reinterpret_cast<double*>(controller + 0x198) == 0, "no vertical impulse");
        processImpact(controller);
        Require(vy == vertical && std::abs(vx - (facing ? -471.0 : 471.0)) <= 1.0, "native physics preserves rising/falling velocity");
        const int calls = validationCalls;
        reusableAt = 0; // even after cooldown, a second air cast must fail
        Require(cast(user, &skill, 15) == 0 && validationCalls == calls, "second air use rejected before MP processing");
        Attach(attach, other, reinterpret_cast<void*>(1));
        Require(cast(user, &skill, 15) == 0, "another controller landing cannot restore charge");
        Attach(attach, controller, nullptr);
        Require(cast(user, &skill, 15) == 0, "detachment cannot restore charge");
    }
    resetAir(); enoughMp = false;
    Require(cast(user, &skill, 15) == 0 && pending == 0, "MP rejection creates no impact");
    enoughMp = true;
    Require(cast(user, &skill, 15) == 1, "MP rejection does not spend air charge");
    resetAir(); reusableAt = now + 1000;
    Require(cast(user, &skill, 15) == 0, "native reuse delay preserved");
    reusableAt = 0; pending = 1;
    Require(cast(user, &skill, 15) == 0, "pending impact blocks cast");
    pending = 0;
    Require(cast(user, &skill, 15) == 1, "failed checks leave air charge available");
    resetAir(); rope = reinterpret_cast<void*>(1);
    Require(cast(user, &skill, 15) == 0, "rope is not airborne casting");
    rope = nullptr;
    Require(cast(user, &skill, 0) == 0 && cast(user, nullptr, 15) == 0, "invalid/unlearned skill rejected");
    Require(cast(user, &skill, 15) == 1, "rope rejection leaves charge available");
    pending = 0; reusableAt = 0;
    AranCombatStep::OnFieldDispose();
    Require(cast(user, &skill, 15) == 1, "map change clears stale state");
    pending = 0; reusableAt = 0;
    *reinterpret_cast<void**>(user + 0x11A4) = other + 0x0C;
    *reinterpret_cast<void**>(other + 0x110) = nullptr;
    Require(cast(user, &skill, 15) == 1, "new controller gets its own air use");
    *reinterpret_cast<void**>(user + 0x11A4) = controller + 0x0C;
    resetAir(); foothold = reinterpret_cast<void*>(1);
    Require(cast(user, &skill, 15) == 1, "ground casting preserved");
    Require(*reinterpret_cast<double*>(controller + 0x190) == -1155.0, "retuned ground impulse preserved");
    resetAir();
    Require(cast(user, &skill, 15) == 1, "landing then immediate jump restores one air cast");
    std::puts("PASS: native airborne cast/physics; both directions; ascent/descent; once per airtime; landing/reset; MP/reuse/pending/rope guards; retuned ground/air impulses");
}

// Run the native branch and table assignment with the same ZF, EBP level,
// and ESI level-data arguments as the real loader. Only secure storage is
// stubbed; the native level indexing and overwrite instructions are executed.
__declspec(naked) void Load(DWORD address, void* levelData, int level, int skill) {
    __asm {
        push ebp
        mov ebp, esp
        push esi
        mov esi, [ebp+0Ch]
        cmp dword ptr [ebp+14h], 21001001
        call dword ptr [ebp+8]
        pop esi
        pop ebp
        ret
    }
}

int main(int argc, char** argv) {
    Require(argc == 2, "original EXE path");
    FILE* file = nullptr;
    Require(fopen_s(&file, argv[1], "rb") == 0, "open client image");
    fseek(file, 0, SEEK_END);
    std::vector<unsigned char> bytes(ftell(file));
    rewind(file);
    Require(fread(bytes.data(), 1, bytes.size(), file) == bytes.size(), "read complete image");
    fclose(file);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    Require(nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.ImageBase == 0x400000,
        "GMS083 image");
    auto* mapped = static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(0x10400000),
        nt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    Require(mapped != nullptr, "allocate isolated native image");
    auto* sections = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        std::memcpy(mapped + sections[i].VirtualAddress, bytes.data() + sections[i].PointerToRawData,
            sections[i].SizeOfRawData);

    auto* setter = mapped + 0x165B1;
    setter[0] = 0xe9;
    const DWORD relative = reinterpret_cast<DWORD>(&StoreX) - reinterpret_cast<DWORD>(setter + 5);
    std::memcpy(setter + 1, &relative, 4);
    mapped[0x3607E7] = 0xc3; // stop before unrelated skill-level finalization
    auto* tableOperand = reinterpret_cast<DWORD*>(mapped + 0x3607D2);
    Require(*tableOperand == 0xBDB55C, "native Combat Step table address");
    *tableOperand += 0x10000000;
    FlushInstructionCache(GetCurrentProcess(), mapped, nt->OptionalHeader.SizeOfImage);
    const DWORD branch = reinterpret_cast<DWORD>(mapped + 0x3607CA);
    alignas(4) unsigned char levelData[0x148]{};
    auto* x = reinterpret_cast<int*>(levelData + 0x13C);
    for (int level = 1; level <= 15; ++level) {
        *x = 2000;
        Load(branch, levelData, level, 21001001);
        Require(*x == 625 + 25 * level, "reproduce native WZ override at every skill level");
    }
    Require(setterCalls == 15, "native overwrite executed");
    *tableOperand -= 0x10000000;

    Require(!AranCombatStep::Install(nullptr), "reject null module");
    mapped[0x3607CB] ^= 1;
    Require(!AranCombatStep::Install(reinterpret_cast<HMODULE>(mapped)) && mapped[0x3607CA] == 0x75,
        "reject changed jump target without writing");
    mapped[0x3607CB] ^= 1;
    mapped[0x3607B5] ^= 1;
    Require(!AranCombatStep::Install(reinterpret_cast<HMODULE>(mapped)) && mapped[0x3607CA] == 0x75,
        "reject different skill without writing");
    mapped[0x3607B5] ^= 1;
    std::vector<unsigned char> before(mapped, mapped + nt->OptionalHeader.SizeOfImage);
    Require(AranCombatStep::Install(reinterpret_cast<HMODULE>(mapped)), "install");
    Require(AranCombatStep::Install(reinterpret_cast<HMODULE>(mapped)), "idempotent install");
    size_t changed = 0;
    for (size_t i = 0; i < before.size(); ++i) {
        if (before[i] != mapped[i]) {
            ++changed;
            Require(i == 0x3607CA || (i >= 0x56902E && i < 0x569033) ||
                (i >= 0x56D9D3 && i < 0x56D9D9) || (i >= 0x56DAFA && i < 0x56DAFF) ||
                (i >= 0x5B49C9 && i < 0x5B49CF), "only declared native sites change");
        }
    }
    Require(changed > 1 && mapped[0x3607CA] == 0xeb, "resource and air hooks installed");

    const int speeds[] = {0, 750, 780, 808, 837, 866, 895, 923, 953, 981, 1010, 1039, 1068, 1097, 1126, 1155};
    for (int skill : {21001001, 21000000, 21001003, 4121006}) {
        for (int level = 1; level <= 15; ++level) {
            std::memset(levelData, 0xa5, sizeof(levelData));
            *x = speeds[level];
            unsigned char saved[sizeof(levelData)];
            std::memcpy(saved, levelData, sizeof(saved));
            Load(branch, levelData, level, skill);
            Require(std::memcmp(saved, levelData, sizeof(saved)) == 0, "WZ level data preserved");
        }
    }
    Require(setterCalls == 15, "native override never called after patch");
    std::puts("PASS: native overwrite reproduced for 15 levels; 60 patched skill/level cases; signature guards; scoped patch; idempotence");
    AirCases(mapped);
    VirtualFree(mapped, 0, MEM_RELEASE);
}
