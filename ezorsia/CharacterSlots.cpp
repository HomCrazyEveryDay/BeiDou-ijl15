#include "stdafx.h"
#include "CharacterSlots.h"
#include "ClientLog.h"
#include <cstring>
#include <initializer_list>
#include <vector>

namespace {
#ifdef CHARACTER_SLOTS_TEST
constexpr DWORD Native(DWORD address) { return address + 0x10000000; }
#else
constexpr DWORD Native(DWORD address) { return address; }
#endif
// Match server Client.MAX_CHARACTER_SLOTS. The final page draws three records,
// so allocate one empty padding record without granting a 21st character slot.
constexpr DWORD kMaxSlots = 20;
constexpr DWORD kRecordCount = ((kMaxSlots + 2) / 3) * 3;
constexpr DWORD kLastRecord = (kMaxSlots - 1) * 0x2AC;
constexpr DWORD kLastRank = (kMaxSlots - 1) * 0x10;

// CUIAvatar is a native singleton (00BEDA4C). Its inline pointer table only has
// 15 entries, immediately followed by button objects at +AC. Redirect the
// initialization and all six readers to this borrowed table; never enlarge the
// inline array over those buttons. Native OnCreate refreshes every entry.
DWORD g_records[kRecordCount]{};
DWORD g_initReturn = Native(0x0060464D);
DWORD g_keyReturn = Native(0x00604FEA);
DWORD g_previousPageReturn = Native(0x006051F2);
DWORD g_drawReturn = Native(0x0060598C);
DWORD g_selectReturn = Native(0x006059B6);
DWORD g_labelReturn = Native(0x00605C0B);
DWORD g_labelAltReturn = Native(0x00605E87);
bool g_installed = false;

__declspec(naked) void InitializePointers() {
    __asm {
        mov eax, offset g_records
        mov edx, kRecordCount
        jmp dword ptr [g_initReturn]
    }
}
__declspec(naked) void KeyRecord() {
    __asm {
        mov ecx, dword ptr [g_records + eax * 4]
        cmp dword ptr [ecx], 0
        jmp dword ptr [g_keyReturn]
    }
}
__declspec(naked) void PreviousPageRecord() {
    __asm {
        // Original this is the UI's +4 interface; +74 addresses slot index+2.
        lea eax, [g_records + eax * 4 + 8]
    scan:
        mov edi, dword ptr [eax]
        cmp dword ptr [edi], 0
        jne done
        dec ecx
        sub eax, 4
        test ecx, ecx
        jge scan
    done:
        jmp dword ptr [g_previousPageReturn]
    }
}
__declspec(naked) void DrawRecord() {
    __asm {
        push dword ptr [g_records + eax * 4]
        mov ecx, edi
        jmp dword ptr [g_drawReturn]
    }
}
__declspec(naked) void SelectRecord() {
    __asm {
        mov eax, dword ptr [g_records + edi * 4]
        cmp dword ptr [eax], 0
        jmp dword ptr [g_selectReturn]
    }
}
__declspec(naked) void LabelRecord() {
    __asm {
        mov eax, dword ptr [g_records + eax * 4]
        push ecx
        jmp dword ptr [g_labelReturn]
    }
}
__declspec(naked) void LabelRecordAlt() {
    __asm {
        mov eax, dword ptr [g_records + eax * 4]
        push ecx
        jmp dword ptr [g_labelAltReturn]
    }
}

struct Patch {
    DWORD address;
    std::vector<BYTE> before;
    std::vector<BYTE> after;
    DWORD protection = 0;
};

void Immediate(std::vector<Patch>& patches, DWORD address,
    std::initializer_list<BYTE> before, size_t offset, size_t size, DWORD value) {
    address = Native(address);
    Patch patch{address, before, before};
    std::memcpy(patch.after.data() + offset, &value, size);
    patches.push_back(patch);
}

void Jump(std::vector<Patch>& patches, DWORD address,
    std::initializer_list<BYTE> before, void (*target)()) {
    address = Native(address);
    Patch patch{address, before, std::vector<BYTE>(before.size(), 0x90)};
    patch.after[0] = 0xE9;
    const DWORD relative = reinterpret_cast<DWORD>(target) - (address + 5);
    std::memcpy(patch.after.data() + 1, &relative, sizeof(relative));
    patches.push_back(patch);
}

std::vector<Patch> BuildPatches() {
    std::vector<Patch> patches;
    // Cash shop purchase, Maple Life purchase, successful expansion response,
    // and tooltip's maximum argument. Keep all native branch conditions.
    Immediate(patches, 0x0046C751, {0x83,0xBE,0x90,0x04,0,0,0x0F}, 6, 1, kMaxSlots);
    Immediate(patches, 0x0046DF7C, {0x83,0xF9,0x0F}, 2, 1, kMaxSlots);
    Immediate(patches, 0x0047AC37, {0x83,0xF8,0x0F}, 2, 1, kMaxSlots);
    Immediate(patches, 0x008F1B3D, {0x6A,0x0F}, 1, 1, kMaxSlots);

    // CLogin's native ZArrays: character records, ranks, and delete flags.
    // Their constructors/destructors already use the stored array length.
    for (DWORD address : {0x005F49B1, 0x005F49C2, 0x005F49D3,
                          0x005F56E6, 0x005F56F7, 0x005F5708}) {
        Immediate(patches, address, {0x6A,0x0F}, 1, 1, kRecordCount);
    }
    // Selection, login/delete requests, incoming character-list decoding,
    // and finding a character in create/delete responses.
    for (DWORD address : {0x005F5107, 0x005F5169, 0x005F72A7, 0x005F7C78}) {
        Immediate(patches, address, {0x83,0xF8,0x0F}, 2, 1, kMaxSlots);
    }
    Immediate(patches, 0x005F9B1F, {0x83,0x7D,0xF0,0x0F}, 3, 1, kMaxSlots);
    Immediate(patches, 0x005F9E1C, {0x83,0xF9,0x0F}, 2, 1, kMaxSlots);
    Immediate(patches, 0x005FA2F4, {0x83,0xF9,0x0F}, 2, 1, kMaxSlots);
    // A full list is determined by its last record, not just the slot count.
    Immediate(patches, 0x005F7EA0, {0x39,0x98,0x68,0x25,0,0}, 2, 4, kLastRecord);
    // Deletion compacts records and rank data then clears the final entry.
    Immediate(patches, 0x005F9E4B, {0x83,0xFB,0x0E}, 2, 1, kMaxSlots - 1);
    Immediate(patches, 0x005F9E89, {0x3D,0x68,0x25,0,0}, 1, 4, kLastRecord);
    Immediate(patches, 0x005F9E99, {0x83,0xA0,0x68,0x25,0,0,0}, 2, 4, kLastRecord);
    Immediate(patches, 0x005F9EAB, {0xBF,0xE0,0,0,0}, 1, 4, kLastRank);
    Immediate(patches, 0x006059AA, {0x83,0xFF,0x0F}, 2, 1, kMaxSlots);

    Jump(patches, 0x00604647, {0x6A,0x0F,0x8D,0x46,0x70,0x5A}, InitializePointers);
    Jump(patches, 0x00604FE3, {0x8B,0x4C,0x86,0x6C,0x83,0x39,0}, KeyRecord);
    // Move the complete backward scan: its original loop jumps to 6051E3,
    // which would otherwise land in the middle of a replacement jump.
    Jump(patches, 0x006051DF, {0x8D,0x44,0x86,0x74,0x8B,0x38,0x83,0x3F,0,
        0x75,0x08,0x49,0x83,0xE8,0x04,0x85,0xC9,0x7D,0xF1}, PreviousPageRecord);
    Jump(patches, 0x00605986, {0xFF,0x74,0x87,0x70,0x8B,0xCF}, DrawRecord);
    Jump(patches, 0x006059AF, {0x8B,0x44,0xBE,0x70,0x83,0x38,0}, SelectRecord);
    Jump(patches, 0x00605C06, {0x8B,0x44,0x83,0x70,0x51}, LabelRecord);
    Jump(patches, 0x00605E82, {0x8B,0x44,0x83,0x70,0x51}, LabelRecordAlt);
    return patches;
}
}

bool CharacterSlots::Install() {
    if (g_installed) return true;
    auto patches = BuildPatches();
    // Validate EVERY site before changing any code. A mismatch must not leave
    // the shop accepting 20 while the login arrays still only hold 15.
    for (const auto& patch : patches) {
        if (std::memcmp(reinterpret_cast<void*>(patch.address),
            patch.before.data(), patch.before.size()) != 0) {
            ClientLog::Append(ClientLog::Component::Lifecycle,
                "character_slots_mismatch address=%08lX", patch.address);
            return false;
        }
    }
    size_t writable = 0;
    for (; writable < patches.size(); ++writable) {
        auto& patch = patches[writable];
        if (!VirtualProtect(reinterpret_cast<void*>(patch.address), patch.after.size(),
            PAGE_EXECUTE_READWRITE, &patch.protection)) break;
    }
    const bool success = writable == patches.size();
    if (success) {
        for (const auto& patch : patches) {
            std::memcpy(reinterpret_cast<void*>(patch.address), patch.after.data(), patch.after.size());
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(patch.address), patch.after.size());
        }
    }
    // Multiple sites can share a page; restore protections in reverse order.
    while (writable > 0) {
        const auto& patch = patches[--writable];
        DWORD ignored;
        VirtualProtect(reinterpret_cast<void*>(patch.address), patch.after.size(), patch.protection, &ignored);
    }
    g_installed = success;
    ClientLog::Append(ClientLog::Component::Lifecycle,
        "character_slots_install success=%d max=%lu records=%lu", success, kMaxSlots, kRecordCount);
    return success;
}
