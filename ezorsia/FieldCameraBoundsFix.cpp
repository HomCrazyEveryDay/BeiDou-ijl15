#include "stdafx.h"
#include "FieldCameraBoundsFix.h"
#include <cstring>

namespace {
BYTE* installedClient = nullptr;
DWORD boundsContinue = 0, releaseCamera = 0;

// CAnimationDisplayer::Update asks for a CField after updating screen shake.
// During SET_CASH_SHOP the current stage is no longer a field. Skip ONLY the
// field rectangle/clamping block in that case, before constructing its VARIANT.
// The camera reference still follows native cleanup at 4378E6; shake decay,
// zero-offset reset and all subsequent animation updates remain native.
__declspec(naked) void GuardFieldBounds() {
    __asm {
        pushfd
        test eax, eax
        jz missingField
        popfd
        push 3
        mov esi, eax
        push 1
        jmp dword ptr [boundsContinue]
    missingField:
        popfd
        jmp dword ptr [releaseCamera]
    }
}

bool Read(const BYTE* address, void* output, SIZE_T size) {
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), address, output, size, &copied)
        && copied == size;
}
template<SIZE_T N> bool Matches(const BYTE* address, const BYTE (&expected)[N]) {
    BYTE actual[N];
    return Read(address, actual, N) && !std::memcmp(actual, expected, N);
}
}

bool FieldCameraBoundsFix::Install(HMODULE client) {
    auto* base = reinterpret_cast<BYTE*>(client);
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS32 nt{};
    if (!base || !Read(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE
        || dos.e_lfanew < 0x40 || dos.e_lfanew > 0x1000
        || !Read(base + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE
        || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC
        || nt.FileHeader.TimeDateStamp != 0x4B7C15C9 || nt.OptionalHeader.SizeOfImage != 0xA94000) return false;

    BYTE patch[] = {0xe9,0,0,0,0,0x90};
    const DWORD relative = reinterpret_cast<DWORD>(&GuardFieldBounds) - reinterpret_cast<DWORD>(base + 0x37846);
    std::memcpy(patch + 1, &relative, sizeof(relative));
    if (installedClient) return base == installedClient && Matches(base + 0x37841, patch);

    const BYTE original[] = {0x6a,3,0x8b,0xf0,0x6a,1};
    const BYTE lookup[] = {0xe8,0xcb,1,0,0};
    const BYTE bounds[] = {0x8d,0x4d,0xbc,0x81,0xc6,0xf0,0,0,0,0xe8,0x56,0xb7,0xfc,0xff};
    const BYTE cleanup[] = {0x83,0x4d,0xfc,0xff,0x8d,0x4d,0xf0,0xe8,0xac,0xdd,0xfd,0xff,0x8b,0x7d,0xec};
    const BYTE getField[] = {0x56,0x8b,0x35,0xd4,0xde,0xbe,0,0x85,0xf6,0x74,0x16,
        0x8b,0x46,4,0x8d,0x4e,4,0x68,0x58,0xd7,0xbe,0,0xff,0x50,0x48,0x85,0xc0,
        0x74,4,0x8b,0xc6,0x5e,0xc3,0x33,0xc0,0x5e,0xc3};
    if (!Matches(base + 0x3783c, lookup) || !Matches(base + 0x37841, original)
        || !Matches(base + 0x37847, bounds) || !Matches(base + 0x378e6, cleanup)
        || !Matches(base + 0x37a0c, getField)) return false;

    auto* site = base + 0x37841;
    DWORD protection = 0, ignored = 0;
    if (!VirtualProtect(site, sizeof(patch), PAGE_EXECUTE_READWRITE, &protection)) return false;
    boundsContinue = reinterpret_cast<DWORD>(base + 0x37847);
    releaseCamera = reinterpret_cast<DWORD>(base + 0x378e6);
    std::memcpy(site, patch, sizeof(patch));
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), site, sizeof(patch)) != FALSE;
    if (!flushed) {
        std::memcpy(site, original, sizeof(original));
        FlushInstructionCache(GetCurrentProcess(), site, sizeof(original));
    }
    if (!VirtualProtect(site, sizeof(patch), protection, &ignored)) {
        std::memcpy(site, original, sizeof(original));
        FlushInstructionCache(GetCurrentProcess(), site, sizeof(original));
        VirtualProtect(site, sizeof(patch), protection, &ignored);
        return false;
    }
    if (!flushed) return false;
    installedClient = base;
    return true;
}
