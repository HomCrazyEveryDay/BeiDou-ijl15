#include "../ezorsia/stdafx.h"
#include "../ezorsia/AranFirstAttack.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static void Require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
struct User { void** vtable; int job; };
static int __fastcall GetJob(User* user, void*) { return user->job; }

// Execute the installed cave AND the EXE's original left/right mirroring.
// The native frame is isolated test memory; no client process is launched.
__declspec(naked) void Run(DWORD address, void* frame, User* user, RECT* result) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        mov esi, ebp
        mov ebp, [esi+0Ch]
        mov ebx, [esi+10h]
        mov edi, [esi+14h]
        mov eax, [ebp-6Ch]
        mov edx, [ebp-64h]
        call dword ptr [esi+8]
        mov eax, [ebp-6Ch]
        mov [edi], eax
        mov eax, [ebp-68h]
        mov [edi+4], eax
        mov eax, [ebp-64h]
        mov [edi+8], eax
        mov eax, [ebp-60h]
        mov [edi+0Ch], eax
        mov ebp, esi
        lea esp, [ebp-0Ch]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}

int main(int argc, char** argv) {
    Require(argc == 2, "original EXE path");
    FILE* file = nullptr;
    Require(fopen_s(&file, argv[1], "rb") == 0, "read original EXE");
    fseek(file, 0, SEEK_END);
    std::vector<unsigned char> bytes(ftell(file));
    rewind(file);
    Require(fread(bytes.data(), 1, bytes.size(), file) == bytes.size(), "complete image");
    fclose(file);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
    Require(nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.ImageBase == 0x400000, "GMS083 image");
    auto* mapped = static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(0x10400000),
        nt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    Require(mapped != nullptr, "isolated image allocation");
    auto* sections = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        memcpy(mapped + sections[i].VirtualAddress, bytes.data() + sections[i].PointerToRawData, sections[i].SizeOfRawData);

    auto* site = reinterpret_cast<unsigned char*>(0x10951417);
    unsigned char saved[6]; memcpy(saved, site, sizeof(saved));
    site[1] ^= 1;
    Require(!AranFirstAttack::Install() && site[0] == saved[0], "unknown EXE rejected without patching");
    memcpy(site, saved, sizeof(saved));
    Require(AranFirstAttack::Install() && AranFirstAttack::Install(), "install and repeat");
    // Relocate only this block's SetRect import and stop after native mirroring.
    Require(*reinterpret_cast<DWORD*>(0x1095142F) == 0x00BF040C, "native SetRect import");
    *reinterpret_cast<DWORD*>(0x1095142F) = 0x10BF040C;
    *reinterpret_cast<void**>(0x10BF040C) = reinterpret_cast<void*>(&SetRect);
    *reinterpret_cast<unsigned char*>(0x10951433) = 0xC3;
    FlushInstructionCache(GetCurrentProcess(), mapped, nt->OptionalHeader.SizeOfImage);

    void* vtable[17]{};
    vtable[16] = reinterpret_cast<void*>(&GetJob);
    unsigned cases = 0;
    for (int job : {2000, 2100, 2110, 2111, 2112, 110, 112, 132, 2218})
    for (int weapon : {0, 43, 44, 45})
    for (int skill : {0, 21000002, 21100001, 21110007, 21120009, 21120005, 21120006})
    for (int left : {-65, -110, -179, -180, -250})
    for (int facing : {0, 1}) {
        alignas(16) unsigned char storage[0x100]{};
        auto* frame = storage + 0x80;
        RECT input{left, -73, 10, 9};
        memcpy(frame - 0x6C, &input, sizeof(input));
        *reinterpret_cast<int*>(frame - 0x10) = skill;
        *reinterpret_cast<int*>(frame - 0x4C) = weapon;
        *reinterpret_cast<int*>(frame - 0x18) = facing;
        // A sentinel outside the rectangle models the independently chosen target count.
        *reinterpret_cast<int*>(frame - 0x1C) = 7;
        User user{vtable, job};
        RECT result{};
        Run(0x10951417, frame, &user, &result);
        bool extend = (job == 2100 || job == 2110 || job == 2111 || job == 2112)
            && weapon == 44 && skill == 0 && left > -180;
        RECT expected = input;
        if (extend) expected.left = -180;
        if (!facing) { expected.left = -input.right; expected.right = extend ? 180 : -input.left; }
        Require(memcmp(&result, &expected, sizeof(result)) == 0, "range, facing, vertical and rear bounds");
        Require(*reinterpret_cast<int*>(frame - 0x1C) == 7, "target count preserved");
        Require(*reinterpret_cast<int*>(frame - 0x10) == skill, "skill unchanged");
        ++cases;
    }
    std::printf("PASS: %u native range cases, both facings, job/weapon/skill isolation, existing longer reach, signature guard\n", cases);
}
