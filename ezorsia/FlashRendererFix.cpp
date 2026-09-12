#include "stdafx.h"
#include "FlashRendererFix.h"
#include "CrashReporter.h"
#include "detours.h"
#include <cstring>

namespace {
using Render = int(__thiscall*)(void*);
Render g_render = nullptr;
HMODULE g_installed = nullptr;

bool MatchesImage(HMODULE module, DWORD timestamp, DWORD imageSize)
{
    auto base = reinterpret_cast<const BYTE*>(module);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE
        && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386
        && nt->FileHeader.TimeDateStamp == timestamp
        && nt->OptionalHeader.SizeOfImage == imageSize;
}

int __fastcall RenderReadyMovie(void* self, void*)
{
    auto fields = static_cast<DWORD*>(self);
    auto callback = reinterpret_cast<const char*>(fields[3]);
    HMODULE flash = nullptr;
    // Check the callback's loaded image, not a possibly stale HMODULE stored
    // in the native object. Other renderer versions retain native behavior.
    if (callback && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, callback, &flash)
        && MatchesImage(flash, 0x486B58AE, 0x82000)) {
        auto base = reinterpret_cast<const BYTE*>(flash);
        const BYTE expected[] = {0x53,0x55,0x56,0x8B,0xF1,0x8B,0x4E,0x08,0x8B,0x01};
        if (callback == reinterpret_cast<const char*>(base + 0x14F0)
            && std::memcmp(base + 0x1290, expected, sizeof(expected)) == 0) {
            // LoadMediaFile leaves either movie definition or instance null
            // before loading or on failure. Native RenderFlash dereferences
            // both unconditionally, catches the AV, and returns failure (1).
            // Return that same failure BEFORE accessing either null object.
            auto movie = reinterpret_cast<const DWORD*>(base + 0x6E088);
            if (!movie[0] || !movie[1]) return 1;
        }
    }
    return g_render(self);
}
}

bool FlashRendererFix::Install(HMODULE gr2d)
{
    if (g_installed) return g_installed == gr2d;
    if (!MatchesImage(gr2d, 0x4B7C13FD, 0x40000)) return false;
    auto base = reinterpret_cast<BYTE*>(gr2d);
    // Same ECX object is cleaned up twice consecutively. The first cleanup
    // releases Flash and FreeLibrary; the second calls the unloaded export.
    const BYTE cleanup[] = {0x8D,0x9E,0x28,0x01,0,0,0x8B,0xCB,0x89,0x38,
        0xE8,0x5E,0x45,0,0,0x8B,0xCB,0xE8,0x57,0x45,0,0};
    // Exclude the relocated absolute SEH address in the first instruction.
    const BYTE render[] = {0xE8,0x40,0x29,0x02,0,0x51,0x8B,0x41,0x0C,
        0x83,0x65,0xFC,0,0x85,0xC0,0x53,0x56,0x57,0x89,0x65,0xF0};
    if (std::memcmp(base + 0x35DF, cleanup, sizeof(cleanup)) != 0
        || std::memcmp(base + 0x7CAB, render, sizeof(render)) != 0) return false;

    DWORD oldProtect = 0;
    if (!VirtualProtect(base + 0x35F0, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
    g_render = reinterpret_cast<Render>(base + 0x7CA6);
    LONG result = DetourTransactionBegin();
    if (result == NO_ERROR) {
        result = DetourUpdateThread(GetCurrentThread());
        if (result == NO_ERROR)
            result = DetourAttach(reinterpret_cast<PVOID*>(&g_render), RenderReadyMovie);
        if (result == NO_ERROR) result = DetourTransactionCommit();
        else DetourTransactionAbort();
    }
    if (result == NO_ERROR) {
        std::memset(base + 0x35F0, 0x90, 5);
        FlushInstructionCache(GetCurrentProcess(), base + 0x35F0, 5);
        g_installed = gr2d;
    }
    DWORD ignored = 0;
    VirtualProtect(base + 0x35F0, 5, oldProtect, &ignored);
    return result == NO_ERROR;
}

void FlashRendererFix::Install()
{
    // Called after the native graphics factory returns. Gr2D DllMain calls
    // back into EXE globals, so loading it early at process entry is unsafe.
    // Acquire only an existing module reference; never trigger initialization.
    if (g_installed) return;
    HMODULE module = nullptr;
    const BOOL acquired = GetModuleHandleExW(0, L"Gr2D_DX8.dll", &module);
    const DWORD error = acquired ? ERROR_SUCCESS : GetLastError();
    const bool installed = acquired && Install(module);
    if (module && !installed) FreeLibrary(module);
    CrashReporter::RecordEvent("flash.fix",
        "install result=%d gr2d=%p error=%lu stage=after_native_graphics_init",
        installed ? 1 : 0, module, error);
}
