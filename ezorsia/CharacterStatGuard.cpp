#include "stdafx.h"
#include "CharacterStatGuard.h"
#include "CrashReporter.h"
#include <cstring>

namespace {
using OnStatChanged = void(__thiscall*)(void*, void*);
OnStatChanged nativeHandler = nullptr;
BYTE* installedClient = nullptr;
LONG missingReports = 0;

void __fastcall ApplyToExistingCharacter(void* context, void*, void* packet) {
    // CWvsContext::GetCharacterData (00425D0B) reads this exact ZRef field.
    // The 0x1F dispatcher can receive stat packets before character
    // initialization or after cleanup. Packets have independent decoded buffers;
    // returning here does not leave the next network packet misaligned.
    if (*reinterpret_cast<void**>(static_cast<BYTE*>(context) + 0x20b8)) {
        nativeHandler(context, packet);
        return;
    }
    if (InterlockedIncrement(&missingReports) <= 8)
        CrashReporter::RecordEvent("character.stat", "missing_character opcode=001F context=%p packet=%p action=discard", context, packet);
}
}

bool CharacterStatGuard::Install(HMODULE client) {
    auto base = reinterpret_cast<BYTE*>(client);
    if (installedClient) return base == installedClient;
    if (!base) return false;
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew > 0x1000) return false;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || nt->FileHeader.TimeDateStamp != 0x4B7C15C9 || nt->OptionalHeader.SizeOfImage != 0xA94000) return false;
    const BYTE call[] = {0xe8,0x0f,0x81,0x01,0x00};
    const BYTE lookup[] = {0x8b,0x81,0xb8,0x20,0,0};
    const BYTE fault[] = {0x8b,0x70,0x04,0x8d,0x4d,0xcc,0xe8,0x15,0x8e,0xa0,0xff,0xff,0x76,0x35};
    if (std::memcmp(base + 0x607a3e, call, sizeof(call))
        || std::memcmp(base + 0x25d0f, lookup, sizeof(lookup))
        || std::memcmp(base + 0x61fb97, fault, sizeof(fault))) return false;
    auto site = base + 0x607a3e;
    DWORD protection = 0;
    if (!VirtualProtect(site, sizeof(call), PAGE_EXECUTE_READWRITE, &protection)) return false;
    nativeHandler = reinterpret_cast<OnStatChanged>(base + 0x61fb52);
    const DWORD displacement = reinterpret_cast<DWORD>(&ApplyToExistingCharacter) - reinterpret_cast<DWORD>(site) - 5;
    std::memcpy(site + 1, &displacement, sizeof(displacement));
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), site, sizeof(call)) != FALSE;
    DWORD ignored = 0;
    const bool restored = VirtualProtect(site, sizeof(call), protection, &ignored) != FALSE;
    installedClient = base;
    CrashReporter::RecordEvent("character.stat", "install result=1 flush=%d protectionRestored=%d", flushed, restored);
    return true;
}
