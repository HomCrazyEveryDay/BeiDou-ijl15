#pragma once
#include <windows.h>
#include <cstring>

namespace NameTagOriginFix {

// The v83 name-tag compositor aligns w/c by max(originY)-originY, but
// hardcodes y=0 for e at 005F0D2E. Modern unequal-origin frames expose a
// seam (370: 10-7=3; 375: 6-2=4). Keep the native canvas, bounds, tiling,
// text and HRESULT; correct only the right-hand Copy call's Y argument.
using GetOriginY = int(__thiscall*)(void*);
static auto getOriginY = reinterpret_cast<GetOriginY>(0x0040F0C2);
static const DWORD copySite = 0x005F0D36;
static const BYTE originalCall[] = {0xff,0x90,0x80,0x00,0x00,0x00};

__declspec(naked) static void CopyRight() {
    __asm {
        // Stack: return, destination, x, y, source, VARIANT alpha.
        // EBP remains the native compositor frame; +20h is max origin Y.
        push eax
        push ecx
        push edx
        mov ecx, [esp + 1Ch]
        call dword ptr [getOriginY]
        mov edx, [ebp + 20h]
        sub edx, eax
        mov [esp + 18h], edx
        pop edx
        pop ecx
        pop eax
        // Tail-call the original IWzCanvas::Copy (stdcall, 32 bytes).
        jmp dword ptr [eax + 80h]
    }
}

static bool InstallPatch() {
    const BYTE context[] = {0x6a,0x00,0xa5,0x51,0xff,0x75,0x28,0xa5};
    const BYTE originGetter[] = {0x55,0x8b,0xec,0x51,0x56,0x8b,0xf1,0x8b,0x06,
        0x8d,0x4d,0xfc,0x51,0x56,0xff,0x50,0x74};
    auto site = reinterpret_cast<BYTE*>(copySite);
    if (std::memcmp(site - sizeof(context), context, sizeof(context))
        || std::memcmp(site, originalCall, sizeof(originalCall))
        || std::memcmp(reinterpret_cast<void*>(getOriginY), originGetter, sizeof(originGetter))) return false;
    BYTE patch[] = {0xe8,0,0,0,0,0x90};
    const DWORD displacement = reinterpret_cast<DWORD>(CopyRight) - (copySite + 5);
    std::memcpy(patch + 1, &displacement, sizeof(displacement));
    DWORD protection = 0, ignored = 0;
    if (!VirtualProtect(site, sizeof(patch), PAGE_EXECUTE_READWRITE, &protection)) return false;
    std::memcpy(site, patch, sizeof(patch));
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch)) != FALSE;
    if (!flushed) {
        std::memcpy(site, originalCall, sizeof(patch));
        FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch));
    }
    if (!VirtualProtect(site, sizeof(patch), protection, &ignored)) {
        // The failed protection restore leaves the writable page available.
        // Roll back the bytes as well, and retry restoring original protection.
        std::memcpy(site, originalCall, sizeof(patch));
        FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch));
        VirtualProtect(site, sizeof(patch), protection, &ignored);
        return false;
    }
    return flushed;
}

static bool Install() {
    const auto base = reinterpret_cast<const BYTE*>(GetModuleHandleW(nullptr));
    if (reinterpret_cast<DWORD>(base) != 0x00400000) return false;
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.TimeDateStamp == 0x4B7C15C9
        && nt->OptionalHeader.SizeOfImage == 0xA94000 && InstallPatch();
}
}
