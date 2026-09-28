#include "../ezorsia/stdafx.h"
#include "../ezorsia/AranComboCommand.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static void Require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
struct Levels { int smash, fenrir, tempest; };
static int commands[2];
static int commandCount;
static int __fastcall GetSkillLevel(void* db, void*, const Levels* levels, int skill, void** entry) {
    Require(db == reinterpret_cast<void*>(0x1234) && entry == nullptr, "native skill lookup arguments");
    switch (skill) {
    case 21100004: return levels->smash;
    case 21110004: return levels->fenrir;
    case 21120006: return levels->tempest;
    default: Require(false, "unexpected skill lookup"); return 0;
    }
}
static int* __fastcall AppendCommand(void* list, void*, int index) {
    Require(list == reinterpret_cast<void*>(0x5678) && index == 0, "native command list arguments");
    Require(commandCount < 2, "no duplicate commands");
    return &commands[commandCount++];
}
static void Jump(DWORD address, void* target) {
    auto* code = reinterpret_cast<unsigned char*>(address);
    code[0] = 0xe9;
    *reinterpret_cast<DWORD*>(code + 1) = reinterpret_cast<DWORD>(target) - address - 5;
}

__declspec(naked) void Register(DWORD address, const Levels* levels) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        sub esp, 40h
        mov eax, [ebp+0Ch]
        mov [ebp-14h], eax
        mov dword ptr [ebp-1Ch], 5678h
        mov esi, 1234h
        xor edi, edi
        xor ebx, ebx
        call dword ptr [ebp+8]
        lea esp, [ebp-0Ch]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
__declspec(naked) int SelectSkill(DWORD address, const Levels* levels, void* user) {
    __asm {
        push ebp
        mov ebp, esp
        push ebx
        push esi
        push edi
        sub esp, 40h
        mov ebx, [ebp+0Ch]
        mov esi, [ebp+10h]
        call dword ptr [ebp+8]
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

    // Execute the original registration block, with only external lookups and
    // list allocation stubbed. Stop before the unrelated drain command block.
    Jump(0x107616F6, reinterpret_cast<void*>(&GetSkillLevel));
    Jump(0x10474C2E, reinterpret_cast<void*>(&AppendCommand));
    *reinterpret_cast<unsigned char*>(0x1074C537) = 0xc3;
    // Keep the real combo-cost helper and the unmodified skill selection checks.
    for (DWORD address : {0x1074D36E, 0x1074D392, 0x1074D3BA}) {
        Require(*reinterpret_cast<DWORD*>(address) == 0x00BE78DC, "native skill DB load");
        *reinterpret_cast<DWORD*>(address) += 0x10000000;
    }
    *reinterpret_cast<DWORD*>(0x10BE78DC) = 0x1234;
    const unsigned char selected[] = {0x8b, 0xc7, 0xc3}; // return selected EDI
    const unsigned char none[] = {0x33, 0xc0, 0xc3};
    memcpy(reinterpret_cast<void*>(0x1074D3CF), selected, sizeof(selected));
    memcpy(reinterpret_cast<void*>(0x1074D481), none, sizeof(none));
    FlushInstructionCache(GetCurrentProcess(), mapped, nt->OptionalHeader.SizeOfImage);

    unsigned char user[0x3230]{};
    *reinterpret_cast<int*>(user + 0x3220) = 200;
    Levels onlyTempest{0, 0, 30};
    Register(0x1074C505, &onlyTempest);
    Require(commandCount == 0, "reproduce missing input registration at Tempest 30 and Smash 0");
    Require(SelectSkill(0x1074D352, &onlyTempest, user) == 21120006,
        "native resolver already supports Tempest without lower skills");

    auto* site = reinterpret_cast<unsigned char*>(0x1074C510);
    unsigned char saved[5]; memcpy(saved, site, sizeof(saved));
    site[1] ^= 1;
    Require(!AranComboCommand::Install() && site[1] == (saved[1] ^ 1), "signature mismatch rejected");
    memcpy(site, saved, sizeof(saved));
    Require(AranComboCommand::Install() && AranComboCommand::Install(), "install and repeat");

    unsigned cases = 0;
    for (int smash : {0, 1, 20})
    for (int fenrir : {0, 1, 30})
    for (int tempest : {0, 1, 30})
    for (int combo : {0, 29, 30, 99, 100, 199, 200, 201, 400}) {
        Levels levels{smash, fenrir, tempest};
        commandCount = 0;
        commands[0] = commands[1] = 0;
        Register(0x1074C505, &levels);
        const bool learned = smash > 0 || fenrir > 0 || tempest > 0;
        Require(commandCount == (learned ? 2 : 0), "either learned offensive skill registers both directions");
        if (learned) Require(commands[0] == 9 && commands[1] == 10, "native command slots preserved");
        *reinterpret_cast<int*>(user + 0x3220) = combo;
        const int expected = tempest && combo >= 200 ? 21120006
            : fenrir && combo >= 100 ? 21110004 : smash && combo >= 30 ? 21100004 : 0;
        Require(SelectSkill(0x1074D352, &levels, user) == expected, "native level and combo thresholds preserved");
        Require(levels.smash == smash && levels.fenrir == fenrir && levels.tempest == tempest, "learned levels unmodified");
        Require(*reinterpret_cast<int*>(user + 0x3220) == combo, "registration/selection never spends combo");
        ++cases;
    }
    std::printf("PASS: original failure reproduced; %u native registration/selection cases; zero-level exclusion; 30/100/200 thresholds; signature guard\n", cases);
}
