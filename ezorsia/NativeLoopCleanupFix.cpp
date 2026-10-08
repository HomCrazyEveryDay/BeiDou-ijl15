#include "stdafx.h"
#include "NativeLoopCleanupFix.h"
#include <cstring>

namespace {
BYTE* installedClient = nullptr;

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

bool NativeLoopCleanupFix::Install(HMODULE client) {
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

    const BYTE entry[] = {0xb8,0x2c,0x7e,0xae,0,0xe8,0x3e,0xaf,6,0};
    const BYTE skip[] = {0xe9,0x33,3,0,0,0x90,0x90,0x90,0x90,0x90,0x90,0x90};
    const BYTE cleanup[] = {0xff,0x75,0xcc,0xe8,0x85,0xb4,6,0,0x59};
    if (!Matches(base + 0x5f5c50, entry) || !Matches(base + 0x5f5ca3, skip)
        || !Matches(base + 0x5f6965, cleanup)) return false;

    // This unpacked EXE jumps over the allocation/assignment at 009F5E78,
    // but still frees [ebp-34h] at 009F6965 on every modal-loop return.
    // Initialize only that skipped allocation's pointer. MOV and JMP preserve
    // registers/flags; the original free(nullptr), stop flag and WM_QUIT stay.
    // Relative target remains 009F5FDB. No heap guard or exception suppression.
    const BYTE patch[] = {0xc7,0x45,0xcc,0,0,0,0,0xe9,0x2c,3,0,0};
    auto* site = base + 0x5f5ca3;
    DWORD protection = 0, ignored = 0;
    if (!VirtualProtect(site, sizeof(patch), PAGE_EXECUTE_READWRITE, &protection)) return false;
    std::memcpy(site, patch, sizeof(patch));
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch)) != FALSE;
    if (!flushed) {
        std::memcpy(site, skip, sizeof(skip));
        FlushInstructionCache(GetCurrentProcess(), site, sizeof(skip));
    }
    if (!VirtualProtect(site, sizeof(patch), protection, &ignored)) {
        std::memcpy(site, skip, sizeof(skip));
        FlushInstructionCache(GetCurrentProcess(), site, sizeof(skip));
        VirtualProtect(site, sizeof(patch), protection, &ignored);
        return false;
    }
    if (!flushed) return false;
    installedClient = base;
    return true;
}
