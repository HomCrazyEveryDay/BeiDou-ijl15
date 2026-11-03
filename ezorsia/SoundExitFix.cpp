#include "stdafx.h"
#include "SoundExitFix.h"
#include "ClientLog.h"
#include <cstring>

namespace {
using ShutdownInProgress = BOOLEAN(NTAPI*)();
using DestroySound = void(__thiscall*)(void*);
ShutdownInProgress shuttingDown = nullptr;
DestroySound destroySound = nullptr;
void* soundSingleton = nullptr;
BYTE* installedModule = nullptr;

void __cdecl FinalizeSound() {
    // ExitProcess has already terminated every other thread before DLL CRT
    // callbacks run. SOUND_DX8's singleton destructor sends a stop request to
    // its dead decoder and spins at +1194 waiting for that thread to reply.
    // Do not run this process-owned object's destructor in that phase; Windows
    // reclaims its memory/handles. Ordinary Release, Stop and FreeLibrary still
    // execute the native destructor while the worker is able to respond.
    if (shuttingDown()) {
        ClientLog::Emergency("sound_exit_fix process_teardown=1 native_finalizer_skipped=1");
        return;
    }
    destroySound(soundSingleton);
}

bool Matches(BYTE* base) {
    if (!base) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew > 0x1000) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || nt->FileHeader.TimeDateStamp != 0x4b7c1425 || nt->OptionalHeader.SizeOfImage != 0x24000
        || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return false;
    // Dedicated atexit callback registered at +2D6E, not a general destructor.
    BYTE registration[] = {0x68,0,0,0,0,0xe8,0x62,0xbd,0,0,0x59,0xc3};
    BYTE callback[] = {0xb9,0,0,0,0,0xe9,0xf2,0x0b,0,0};
    BYTE destructor[] = {0xc7,0x01,0,0,0,0,0x8b,0x49,0x58,0x85,0xc9,0x74,0x06,0x8b,0x01,0x6a,0x01,0xff,0x10,0xc3};
    const DWORD finalizer = reinterpret_cast<DWORD>(base + 0x2d7a);
    const DWORD singleton = reinterpret_cast<DWORD>(base + 0x1f888);
    const DWORD vtable = reinterpret_cast<DWORD>(base + 0x18320);
    std::memcpy(registration + 1, &finalizer, 4);
    std::memcpy(callback + 1, &singleton, 4);
    std::memcpy(destructor + 2, &vtable, 4);
    return !std::memcmp(base + 0x2d6e, registration, sizeof(registration))
        && !std::memcmp(base + 0x2d7a, callback, sizeof(callback))
        && !std::memcmp(base + 0x3976, destructor, sizeof(destructor));
}
}

bool SoundExitFix::Install(HMODULE sound) {
    auto* base = reinterpret_cast<BYTE*>(sound);
    BYTE jump[] = {0xe9,0,0,0,0};
    const DWORD relative = reinterpret_cast<DWORD>(&FinalizeSound) - (reinterpret_cast<DWORD>(base) + 0x2d7f);
    std::memcpy(jump + 1, &relative, 4);
    if (base && base == installedModule && !std::memcmp(base + 0x2d7a, jump, sizeof(jump))) return true;
    if (!Matches(base)) return false;
    const auto query = reinterpret_cast<ShutdownInProgress>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlDllShutdownInProgress"));
    if (!query) return false;
    DWORD oldProtect = 0;
    if (!VirtualProtect(base + 0x2d7a, sizeof(jump), PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    BYTE original[sizeof(jump)];
    std::memcpy(original, base + 0x2d7a, sizeof(original));
    shuttingDown = query;
    destroySound = reinterpret_cast<DestroySound>(base + 0x3976);
    soundSingleton = base + 0x1f888;
    std::memcpy(base + 0x2d7a, jump, sizeof(jump));
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), base + 0x2d7a, sizeof(jump)) != FALSE;
    if (!flushed) {
        std::memcpy(base + 0x2d7a, original, sizeof(original));
        FlushInstructionCache(GetCurrentProcess(), base + 0x2d7a, sizeof(original));
    }
    DWORD ignored = 0;
    VirtualProtect(base + 0x2d7a, sizeof(jump), oldProtect, &ignored);
    if (flushed) installedModule = base;
    ClientLog::Emergency("sound_exit_fix install result=%d module=%p", flushed ? 1 : 0, sound);
    return flushed;
}
