#include "stdafx.h"
#include "AranComboCommand.h"
#include <cstring>

namespace {
#ifdef ARAN_COMBO_COMMAND_TEST
constexpr DWORD Native(DWORD address) { return address + 0x10000000; }
#else
constexpr DWORD Native(DWORD address) { return address; }
#endif
constexpr DWORD kCommandRegistrationCall = Native(0x0074C510);
using GetSkillLevel = int(__thiscall*)(void*, void*, int, void**);
const auto NativeGetSkillLevel = reinterpret_cast<GetSkillLevel>(Native(0x007616F6));
bool installed = false;

// Command slots 9/10 (down, forward, attack in either direction) were only
// registered when Combo Smash was learned. After removing the WZ prerequisite
// chain, Fenrir or Tempest alone must also enable this input recognizer.
// This call site only tests > 0; the native resolver at 0x0074D352 still checks
// each skill's actual level and its 30/100/200 combo requirement when casting.
int __fastcall HasOffensiveComboCommand(void* skillDb, void*, void* character,
    int skill, void** entry) {
    const int level = NativeGetSkillLevel(skillDb, character, skill, entry);
    if (level > 0 || skill != 21100004) return level;
    if (NativeGetSkillLevel(skillDb, character, 21110004, nullptr) > 0) return 1;
    return NativeGetSkillLevel(skillDb, character, 21120006, nullptr) > 0 ? 1 : 0;
}
}

bool AranComboCommand::Install() {
    if (installed) return true;
    constexpr unsigned char expected[] = {0xe8, 0xe1, 0x51, 0x01, 0x00};
    auto* site = reinterpret_cast<unsigned char*>(kCommandRegistrationCall);
    if (std::memcmp(site, expected, sizeof(expected)) != 0) return false;
    DWORD previous;
    if (!VirtualProtect(site, sizeof(expected), PAGE_EXECUTE_READWRITE, &previous)) return false;
    unsigned char patch[] = {0xe8, 0, 0, 0, 0};
    const DWORD relative = reinterpret_cast<DWORD>(&HasOffensiveComboCommand) - (kCommandRegistrationCall + 5);
    std::memcpy(patch + 1, &relative, sizeof(relative));
    std::memcpy(site, patch, sizeof(patch));
    FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch));
    DWORD ignored;
    VirtualProtect(site, sizeof(patch), previous, &ignored);
    installed = true;
    return true;
}
