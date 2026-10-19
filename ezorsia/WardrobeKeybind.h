#pragma once
#include "Memory.h"
#include "Client.h"
#include "WardrobeInput.h"
#include <cstring>

namespace WardrobeKeybind {
constexpr int Action = 40, ScanCode = 66;
using Initialize = void(__fastcall*)(void*, void*);
static Initialize initialize = reinterpret_cast<Initialize>(0x00836619);

static int __fastcall DefaultIconIndex(void*, void*, int index) {
    return index == Action || index < 28 ? index : index < 33 ? index + 22 : index + 67;
}

static void __fastcall InitializeTemplates(void* window, void* edx) {
    initialize(window, edx);
    // CUIKeyConfig already allocates 54 entries (12 bytes each), but fills 40.
    // Slot 40 has a native hit-test position at (145,335).
    auto entry = static_cast<unsigned char*>(window) + 0x9dc + Action * 12;
    std::memset(entry, 0, 12);
    entry[0] = 4;
    std::memcpy(entry + 1, &Action, sizeof(Action));
}

static bool Matches(unsigned key, unsigned flags) {
    unsigned scan = (flags >> 16) & 255;
    if (!scan) scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    scan = WardrobeInput::NormalizeScan(scan);
    auto map = *reinterpret_cast<unsigned char**>(0x00BED5A0);
    if (!map || scan >= 90) return false;
    auto entry = map + 4 + scan * 5;
    int action = 0;
    std::memcpy(&action, entry + 1, sizeof(action));
    return entry[0] == 4 && action == Action;
}

static bool Install() {
    const unsigned char init[] = {0x53,0x55,0x56,0x8b,0xe9,0x57,0x33,0xff};
    const unsigned char bound[] = {0x81,0x7d,0xdc,0xf8,0x2b,0xbe,0x00};
    const unsigned char empty[5] = {};
    const unsigned char icon[] = {0x8b,0x44,0x24,0x04,0x83,0xf8,0x1c};
    constexpr DWORD defaultF8 = 0x00BD8BCC + ScanCode * 5;
    if (std::memcmp(reinterpret_cast<void*>(initialize), init, sizeof(init))
        || std::memcmp(reinterpret_cast<void*>(0x00833FAD), bound, sizeof(bound))
        || std::memcmp(reinterpret_cast<void*>(defaultF8), empty, sizeof(empty))
        || (!Client::enableMovementKeyRebind && std::memcmp(reinterpret_cast<void*>(0x00836C82), icon, sizeof(icon)))) return false;
    if (!Memory::SetHook(true, reinterpret_cast<void**>(&initialize), reinterpret_cast<void*>(&InitializeTemplates))) return false;
    Memory::WriteInt(0x00833FB0, 0x00BE2C00); // Include template 40 in native tray drawing.
    Memory::WriteByte(defaultF8, 4);
    Memory::WriteInt(defaultF8 + 1, Action); // Native Restore Defaults; saved bindings stay authoritative.
    // With movement rebinding enabled its existing icon bridge also handles 40.
    if (!Client::enableMovementKeyRebind) Memory::CodeCave(reinterpret_cast<void*>(&DefaultIconIndex), 0x00836C82, 5);
    return true;
}
}
