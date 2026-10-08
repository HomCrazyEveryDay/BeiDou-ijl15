#include "stdafx.h"
#include "CharacterSelectFocusFix.h"
#include "CrashReporter.h"
#include <cstring>

namespace {
using FindRegisteredWindowFn = void*(__thiscall*)(void*, void* const*, void*);
using SetNativeFocusFn = void(__thiscall*)(void*, void*);
FindRegisteredWindowFn findWindow;
SetNativeFocusFn setFocus;
void* registeredWindows;
BYTE* installedClient = nullptr;
volatile LONG recoveries = 0;

void __fastcall FocusRegisteredCharacterWindow(void* manager, void*, void* target) {
    // Button actions can enter a modal loop and destroy the calling window.
    // CWnd::DestroyWnd removes it from this native ZList before releasing it.
    // Compare identities in the registry; never read the stale target itself.
    if (target) {
        void* window = reinterpret_cast<void*>(reinterpret_cast<ULONG_PTR>(target)-4);
        if (!findWindow(registeredWindows, &window, nullptr)) {
            if (InterlockedIncrement(&recoveries) <= 8)
                CrashReporter::RecordEvent("charSelect.focus", "skip focus after window unregistered");
            return;
        }
    }
    setFocus(manager,target);
}
}

bool CharacterSelectFocusFix::Install(HMODULE client) {
    auto* base = reinterpret_cast<BYTE*>(client);
    if (installedClient) return base == installedClient;
    if (!base) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew > 0x1000) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || nt->FileHeader.TimeDateStamp != 0x4B7C15C9 || nt->OptionalHeader.SizeOfImage != 0xA94000) return false;
    const BYTE call[] = {0xe8,0x97,0x1b,0x3e,0,0x5e,0xc2,4,0};
    const BYTE removal[] = {0x56,0x89,0x5e,0x14,0xe8,0x75,0x43,0,0};
    const BYTE registry[] = {0xbe,0x48,0x16,0xbf,0,0x50,0x8b,0xce,0xe8,0x87,8,0,0};
    const BYTE find[] = {0x8b,0x44,0x24,8,0x85,0xc0,0x74,0x0e};
    const BYTE compare[] = {0x8b,0x4c,0x24,4,0x8b,9,0x39,8,0x74,0x1f};
    if (memcmp(base+0x2016c8,call,sizeof(call)) || memcmp(base+0x5e013c,removal,sizeof(removal))
        || memcmp(base+0x5e44c1,registry,sizeof(registry)) || memcmp(base+0x5e4d55,find,sizeof(find))
        || memcmp(base+0x5e4d72,compare,sizeof(compare))) return false;
    findWindow = reinterpret_cast<FindRegisteredWindowFn>(base+0x5e4d55);
    setFocus = reinterpret_cast<SetNativeFocusFn>(base+0x5e3264);
    registeredWindows = base+0x7f1648;
    auto* site = base+0x2016c8;
    DWORD protection = 0;
    if (!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&protection)) return false;
    const DWORD distance = reinterpret_cast<DWORD>(&FocusRegisteredCharacterWindow)-reinterpret_cast<DWORD>(site)-5;
    memcpy(site+1,&distance,sizeof(distance));
    const bool flushed = FlushInstructionCache(GetCurrentProcess(),site,5) != FALSE;
    DWORD ignored=0;
    const bool restored = VirtualProtect(site,5,protection,&ignored) != FALSE;
    installedClient=base;
    CrashReporter::RecordEvent("charSelect.focus", "install result=1 flush=%d protectionRestored=%d",flushed,restored);
    return true;
}
