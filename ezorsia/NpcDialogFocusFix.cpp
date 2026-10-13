#include "stdafx.h"
#include "NpcDialogFocusFix.h"
#include "CrashReporter.h"
#include <cstring>

namespace {
using DestroyWnd = void(__thiscall*)(void*);
using SetNativeFocus = void(__thiscall*)(void*, void*);
DestroyWnd destroyWnd;
SetNativeFocus setFocus;
BYTE** windowManager;
void** localUser;
void** currentStage;
BYTE* installedClient = nullptr;
LONG reports = 0;

void __fastcall CloseNpcDialog(void* dialog, void*) {
    BYTE* manager = *windowManager;
    void* user = *localUser;
    void* stage = *currentStage;
    destroyWnd(dialog);

    // CWndMan::RemoveWnd (009E3454) activates the top remaining CWnd,
    // which can retain keyboard focus after the NPC dialog has disappeared.
    // Use the same stage input handler as clicking the field. Never inspect
    // the destroyed dialog, or take focus from another pending modal dialog.
    if (!manager || !user || !stage || manager != *windowManager
        || user != *localUser || stage != *currentStage
        || *reinterpret_cast<DWORD*>(manager + 0x74) != 0) return;

    void* target = manager + 4;
    void* previous = *reinterpret_cast<void**>(manager + 0x88);
    if (previous == target) return;
    setFocus(manager, target);
    if (InterlockedIncrement(&reports) <= 8)
        CrashReporter::RecordEvent("npc.focus", "closed previous=%p restored=%d", previous,
            *reinterpret_cast<void**>(manager + 0x88) == target ? 1 : 0);
}

bool Read(const BYTE* address, void* output, SIZE_T size) {
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), address, output, size, &copied)
        && copied == size;
}
template<SIZE_T N> bool Matches(const BYTE* address, const BYTE (&expected)[N]) {
    BYTE actual[N];
    return Read(address, actual, N) && std::memcmp(actual, expected, N) == 0;
}
}

bool NpcDialogFocusFix::Install(HMODULE client) {
    auto* base = reinterpret_cast<BYTE*>(client);
    if (installedClient) return base == installedClient;
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS32 nt{};
    if (!base || !Read(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE
        || dos.e_lfanew < 0x40 || dos.e_lfanew > 0x1000
        || !Read(base + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE
        || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC
        || nt.FileHeader.TimeDateStamp != 0x4B7C15C9 || nt.OptionalHeader.SizeOfImage != 0xA94000) return false;

    // Only CUtilDlgEx::SetRet's successful close reaches this call. Number /
    // text validation failures and unrelated windows keep their native focus.
    const BYTE close[] = {0xc7,0x46,0x70,1,0,0,0,0x74,7,0x8b,0xce,0xe8,0xa2,0x47,3,0};
    const BYTE focus[] = {0x55,0x8b,0xec,0x51,0x53,0x56,0x8b,0x75,8};
    const BYTE modal[] = {0x83,0x79,0x74,0,0x53,0x56,0x57};
    if (!Matches(base + 0x5ab8fd, close) || !Matches(base + 0x5e3264, focus)
        || !Matches(base + 0x5e42b7, modal)) return false;

    destroyWnd = reinterpret_cast<DestroyWnd>(base + 0x5e00af);
    setFocus = reinterpret_cast<SetNativeFocus>(base + 0x5e3264);
    windowManager = reinterpret_cast<BYTE**>(base + 0x7ec20c);
    localUser = reinterpret_cast<void**>(base + 0x7ebf98);
    currentStage = reinterpret_cast<void**>(base + 0x7eded4);
    auto* site = base + 0x5ab908;
    BYTE patch[] = {0xe8,0,0,0,0};
    const DWORD relative = reinterpret_cast<DWORD>(&CloseNpcDialog) - reinterpret_cast<DWORD>(site) - 5;
    std::memcpy(patch + 1, &relative, sizeof(relative));
    DWORD protection = 0, ignored = 0;
    if (!VirtualProtect(site, sizeof(patch), PAGE_EXECUTE_READWRITE, &protection)) return false;
    std::memcpy(site, patch, sizeof(patch));
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch)) != FALSE;
    if (!flushed) {
        std::memcpy(site, close + 11, sizeof(patch));
        FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch));
    }
    if (!VirtualProtect(site, sizeof(patch), protection, &ignored)) {
        std::memcpy(site, close + 11, sizeof(patch));
        FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch));
        VirtualProtect(site, sizeof(patch), protection, &ignored);
        return false;
    }
    if (!flushed) return false;
    installedClient = base;
    return true;
}
