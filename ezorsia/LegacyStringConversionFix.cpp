#include "stdafx.h"
#include "LegacyStringConversionFix.h"
#include "detours.h"

namespace {
constexpr int kLegacyCapacity = 0x3fffffff;
decltype(&MultiByteToWideChar) originalToWide = MultiByteToWideChar;
decltype(&WideCharToMultiByte) originalFromWide = WideCharToMultiByte;
HMODULE installedClient = nullptr;

int WINAPI ToWide(UINT codePage, DWORD flags, LPCCH source, int sourceLength,
    LPWSTR destination, int capacity) {
    if (destination && capacity == kLegacyCapacity) {
        // Native callers already queried the size and allocated it, but pass
        // a fictitious 2 GB span. On x86, high addresses make that span wrap;
        // Windows returns ERROR_INSUFFICIENT_BUFFER without writing the name.
        // Query with the same code page/flags, including Wine's ACP mapping.
        const DWORD savedError = GetLastError();
        capacity = originalToWide(codePage, flags, source, sourceLength, nullptr, 0);
        if (!capacity) return 0;
        SetLastError(savedError);
    }
    return originalToWide(codePage, flags, source, sourceLength, destination, capacity);
}

int WINAPI FromWide(UINT codePage, DWORD flags, LPCWCH source, int sourceLength,
    LPSTR destination, int capacity, LPCCH defaultChar, LPBOOL usedDefaultChar) {
    if (destination && capacity == kLegacyCapacity) {
        const DWORD savedError = GetLastError();
        capacity = originalFromWide(codePage, flags, source, sourceLength,
            nullptr, 0, defaultChar, nullptr);
        if (!capacity) return 0;
        SetLastError(savedError);
    }
    return originalFromWide(codePage, flags, source, sourceLength,
        destination, capacity, defaultChar, usedDefaultChar);
}

bool Read(const BYTE* address, void* output, SIZE_T size) {
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), address, output, size, &copied)
        && copied == size;
}
}

bool LegacyStringConversionFix::Install(HMODULE client) {
    if (installedClient) return client == installedClient;
    const auto* base = reinterpret_cast<const BYTE*>(client);
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS32 nt{};
    if (!base || !Read(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE
        || dos.e_lfanew < 0x40 || dos.e_lfanew > 0x1000
        || !Read(base + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE
        || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC
        || nt.FileHeader.TimeDateStamp != 0x4B7C15C9
        || nt.OptionalHeader.SizeOfImage != 0xA94000) return false;

    // Hook the API once: the same legacy conversion is compiled into EXE,
    // PCOM, NameSpace and other lazily loaded WZ components. Patching only
    // StringPool's caller would leave those identical failures untouched.
    LONG error = DetourTransactionBegin();
    if (error != NO_ERROR) return false;
    error = DetourUpdateThread(GetCurrentThread());
    if (error == NO_ERROR)
        error = DetourAttach(reinterpret_cast<PVOID*>(&originalToWide), ToWide);
    if (error == NO_ERROR)
        error = DetourAttach(reinterpret_cast<PVOID*>(&originalFromWide), FromWide);
    if (error == NO_ERROR) error = DetourTransactionCommit();
    else DetourTransactionAbort();
    if (error == NO_ERROR) installedClient = client;
    return error == NO_ERROR;
}
