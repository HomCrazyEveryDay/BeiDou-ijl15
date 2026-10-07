#include "../ezorsia/stdafx.h"
#include "../ezorsia/ComboTempestCritical.h"
#include "../ezorsia/EquipmentCritical.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>

static std::vector<unsigned> encoded;
static void Require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static unsigned __fastcall OriginalFooter(void* mob, void*) {
    Require(mob == reinterpret_cast<void*>(0x123456), "native mob argument");
    return 0x12341234;
}
static void __fastcall Encode4(void*, void*, unsigned value) { encoded.push_back(value); }
static void Jump(DWORD address, void* target) {
    auto* code = reinterpret_cast<unsigned char*>(address);
    code[0] = 0xe9;
    *reinterpret_cast<DWORD*>(code + 1) = reinterpret_cast<DWORD>(target) - address - 5;
}

// Execute the original target damage serialization loop and the installed hook.
// This never starts the client or connects to a game server.
__declspec(naked) unsigned Run(DWORD address, int skill, int count, void* target) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        mov ebx, ebp
        sub esp, 100h
        lea ebp, [esp+90h]
        mov eax, [ebx+0ch]
        mov [ebp-10h], eax
        mov eax, [ebx+10h]
        mov [ebp-18h], eax
        mov esi, [ebx+14h]
        xor edi, edi
        call dword ptr [ebx+8]
        mov ebp, ebx
        lea esp, [ebp-0ch]
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
    auto* site = reinterpret_cast<unsigned char*>(0x10955435);
    unsigned char saved[7]; memcpy(saved, site, sizeof(saved));
    site[1] ^= 1;
    Require(!ComboTempestCritical::Install() && site[0] == saved[0], "mismatched executable rejected without patching");
    memcpy(site, saved, sizeof(saved));
    Require(ComboTempestCritical::Install() && ComboTempestCritical::Install(), "install and repeat");
    Jump(0x106711ac, reinterpret_cast<void*>(&OriginalFooter));
    Jump(0x104065a6, reinterpret_cast<void*>(&Encode4));
    *reinterpret_cast<unsigned char*>(0x10955445) = 0xc3;
    FlushInstructionCache(GetCurrentProcess(), mapped, nt->OptionalHeader.SizeOfImage);

    unsigned target[0x98 / 4]{};
    target[0] = 0x123456;
    for (int i = 0; i < 15; ++i) target[0x18 / 4 + i] = i == 0 ? 650000 : i == 1 ? 0 : 3000000 + i;
    for (int count : {1, 4, 15}) {
        unsigned limit = (1u << count) - 1;
        for (unsigned mask = 0; mask <= limit; ++mask) {
            for (int i = 0; i < 15; ++i) target[0x54 / 4 + i] = mask & (1u << i);
            unsigned before[0x98 / 4]; memcpy(before, target, sizeof(target));
            encoded.clear();
            Run(0x10955415, 21120006, count, target);
            Require(encoded.size() == count + 1, "packet length and line count preserved");
            for (int i = 0; i < count; ++i) Require(encoded[i] == before[0x18 / 4 + i], "damage magnitude and MISS unchanged");
            Require(encoded.back() == (0xac010000 | mask), "all native crit masks survive real serialization loop");
            Require(memcmp(before, target, sizeof(target)) == 0, "target damage and flags unmodified");
        }
    }
    for (int skill : {0, 21100004, 21110004, 3121004, 22181002}) {
        encoded.clear(); Run(0x10955415, skill, 4, target);
        Require(encoded.back() == 0x12341234, "other skills retain original footer");
    }
    EquipmentCritical::Snapshot().store(5);
    for (int skill : {0, 21120006, 3121004, 5221004}) {
        for (int count : {1,4,15}) {
            for (int i=0;i<15;++i) target[0x54/4+i] = i%2;
            encoded.clear(); Run(0x10955415, skill, count, target);
            Require(encoded.size()==count+1 && encoded.back()==(0xcc010000|(0x2aaa&((1u<<count)-1))),
                "equipment flags survive real ranged serialization, including rapid fire and tempest");
        }
    }
    EquipmentCritical::Reset();
    *reinterpret_cast<unsigned char*>(0x1095543c) = 0xc3;
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(0x1095543c), 1);
    for (int count : {0, 16, -1})
        Require(Run(0x10955435, 21120006, count, target) == 0x12341234, "invalid count retains native footer");
    std::puts("PASS: original ranged serialization, 650000 damage, all 1/4/15-line crit masks, MISS, other skills, signature guard");
}
