#include "stdafx.h"
#include "TemporaryStatGuard.h"
#include "CrashReporter.h"
#include <cstring>

namespace {
using Handler = void(__thiscall*)(void*, void*);
Handler give = nullptr, cancel = nullptr;
void** localUser = nullptr;
BYTE* installedClient = nullptr;
LONG missingReports = 0;

void Apply(Handler native, unsigned opcode, void* context, void* packet) {
    // Cash-shop character data/preview is not CUserLocal. Both native handlers
    // dereference CUserLocal after modifying SecondaryStat (A20586 / A20807).
    // Reject before any mutation, not at the fault instruction. Every decoded
    // packet has its own buffer. Channel login restores the authoritative buff
    // snapshot; a field delta must never be queued into a later character.
    if (*localUser) {
        native(context, packet);
        return;
    }
    if (InterlockedIncrement(&missingReports) <= 8)
        CrashReporter::RecordEvent("temporary.stat", "missing_local_user opcode=%04X context=%p packet=%p action=discard", opcode, context, packet);
}
void __fastcall Give(void* context, void*, void* packet) { Apply(give, 0x20, context, packet); }
void __fastcall Cancel(void* context, void*, void* packet) { Apply(cancel, 0x21, context, packet); }
}

bool TemporaryStatGuard::Install(HMODULE client) {
    auto base = reinterpret_cast<BYTE*>(client);
    if (installedClient) return base == installedClient;
    if (!base) return false;
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0x40 || dos->e_lfanew > 0x1000) return false;
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || nt->FileHeader.TimeDateStamp != 0x4B7C15C9 || nt->OptionalHeader.SizeOfImage != 0xA94000) return false;
    const BYTE giveCall[] = {0xe8,0x6e,0x88,0x01,0x00};
    const BYTE cancelCall[] = {0xe8,0xc2,0x8c,0x01,0x00};
    const BYTE getter[] = {0xa1,0x98,0xbf,0xbe,0x00,0xc3};
    const BYTE giveFault[] = {0xe8,0x18,0x87,0xb1,0xff,0x8b,0x30};
    const BYTE cancelFault[] = {0x8b,0xf8,0x8b,0x1f};
    if (std::memcmp(base + 0x607a4b, giveCall, sizeof(giveCall))
        || std::memcmp(base + 0x607a58, cancelCall, sizeof(cancelCall))
        || std::memcmp(base + 0x138c9e, getter, sizeof(getter))
        || std::memcmp(base + 0x620581, giveFault, sizeof(giveFault))
        || std::memcmp(base + 0x620805, cancelFault, sizeof(cancelFault))) return false;
    auto site = base + 0x607a4b;
    constexpr size_t size = 0x12; // Both call sites lie on the same code page.
    DWORD protection = 0;
    if (!VirtualProtect(site, size, PAGE_EXECUTE_READWRITE, &protection)) return false;
    give = reinterpret_cast<Handler>(base + 0x6202be);
    cancel = reinterpret_cast<Handler>(base + 0x62071f);
    localUser = reinterpret_cast<void**>(base + 0x7ebf98);
    const DWORD giveDisplacement = reinterpret_cast<DWORD>(&Give) - reinterpret_cast<DWORD>(site) - 5;
    const DWORD cancelDisplacement = reinterpret_cast<DWORD>(&Cancel) - reinterpret_cast<DWORD>(site + 0xd) - 5;
    std::memcpy(site + 1, &giveDisplacement, 4);
    std::memcpy(site + 0xe, &cancelDisplacement, 4);
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), site, size) != FALSE;
    DWORD ignored = 0;
    const bool restored = VirtualProtect(site, size, protection, &ignored) != FALSE;
    installedClient = base;
    CrashReporter::RecordEvent("temporary.stat", "install result=1 flush=%d protectionRestored=%d", flushed, restored);
    return true;
}
