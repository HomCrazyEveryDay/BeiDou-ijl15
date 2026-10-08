#include "stdafx.h"
#include "PcomExitFix.h"
#include "ClientLog.h"
#include "CrashReporter.h"
#include "detours.h"
#include <cstring>

namespace {
using Destructor = void(__cdecl*)();
BYTE* g_client = nullptr;
BYTE* g_pcom = nullptr;
Destructor g_destroyPool = nullptr;
bool g_installed = false;

constexpr DWORD kPoolDestructor = 0x396D51;
constexpr DWORD kPoolFlags = 0x7F00F0;
constexpr DWORD kPool = 0x7F0B00;
constexpr DWORD kListInitialized = 0x21460;
constexpr DWORD kList = 0x21480;
constexpr DWORD kListDestructor = 0x6C23;

DWORD Read32(const BYTE* address)
{
    DWORD value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}

void SetAddress(BYTE* operand, const BYTE* address)
{
    const DWORD value = reinterpret_cast<DWORD>(address);
    std::memcpy(operand, &value, sizeof(value));
}

bool MatchesImage(const BYTE* base, DWORD timestamp, DWORD imageSize)
{
    if (!base) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < static_cast<LONG>(sizeof(*dos))
        || dos->e_lfanew > 0x1000) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE
        && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386
        && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC
        && nt->FileHeader.TimeDateStamp == timestamp
        && nt->OptionalHeader.SizeOfImage == imageSize;
}

bool UsesNativePool(const BYTE* client, const BYTE* pcom)
{
    return Read32(pcom + 0x212A8) == reinterpret_cast<DWORD>(client + 0x5F2503)
        && Read32(pcom + 0x212AC) == reinterpret_cast<DWORD>(client + 0x5F2525)
        && Read32(pcom + 0x212B0) == reinterpret_cast<DWORD>(client + 0x5F2514);
}

bool MatchesCode(const BYTE* client, const BYTE* pcom)
{
    BYTE pool[] = {0xF6,0x05,0,0,0,0,0x01,0x75,0x11,
        0x80,0x0D,0,0,0,0,0x01,0xB9,0,0,0,0,0xE9,0xF9,0x0D,0,0,0xC3};
    SetAddress(pool + 2, client + kPoolFlags);
    SetAddress(pool + 11, client + kPoolFlags);
    SetAddress(pool + 17, client + kPool);

    BYTE list[] = {0xB9,0,0,0,0,0xE9,0x57,0x08,0,0};
    SetAddress(list + 1, pcom + kList);
    BYTE init[] = {0xF6,0x05,0,0,0,0,0x01,0x75,0x1C,
        0x80,0x0D,0,0,0,0,0x01,0xB9,0,0,0,0,0xE8,0x28,0,0,0,
        0x68,0,0,0,0,0xE8,0xCB,0x96,0,0,0x59,0xB8,0,0,0,0,0xC3};
    SetAddress(init + 2, pcom + kListInitialized);
    SetAddress(init + 11, pcom + kListInitialized);
    SetAddress(init + 17, pcom + kList);
    SetAddress(init + 27, pcom + kListDestructor);
    SetAddress(init + 38, pcom + kList);

    BYTE allocFree[] = {0xFF,0x74,0x24,0x04,0xB9,0,0,0,0,
        0xE8,0x54,0x0B,0xA1,0xFF,0xC2,0x04,0,
        0xFF,0x74,0x24,0x04,0xB9,0,0,0,0,
        0xE8,0xCB,0x0C,0xA1,0xFF,0xC2,0x04,0};
    SetAddress(allocFree + 5, client + kPool);
    SetAddress(allocFree + 22, client + kPool);
    const BYTE clearBegin[] = {0x56,0x57,0x8B,0xF9,0x8B,0x77,0x04,0x85,0xF6,0x74,0x43};
    const BYTE clearEnd[] = {0x83,0x67,0x04,0,0x83,0x67,0x0C,0,0x5D,0x5F,0x5E,0xC3};
    return std::memcmp(client + kPoolDestructor, pool, sizeof(pool)) == 0
        && std::memcmp(client + 0x5F2503, allocFree, sizeof(allocFree)) == 0
        && std::memcmp(pcom + kListDestructor, list, sizeof(list)) == 0
        && std::memcmp(pcom + 0x6BF8, init, sizeof(init)) == 0
        && std::memcmp(pcom + 0x7810, clearBegin, sizeof(clearBegin)) == 0
        && std::memcmp(pcom + 0x7855, clearEnd, sizeof(clearEnd)) == 0;
}

void __cdecl DestroyPoolAfterList()
{
    const bool destroying = (g_client[kPoolFlags] & 1) == 0;
    if (destroying) {
        if ((g_pcom[kListInitialized] & 1) == 0) {
            // Do not call the lazy accessor: shutdown must not create List.wz.
            ClientLog::Emergency("pcom.exit.fix cleanup.skip reason=list_uninitialized");
        } else if (!UsesNativePool(g_client, g_pcom)) {
            ClientLog::Emergency("pcom.exit.fix cleanup.skip reason=allocator_changed");
        } else {
            ClientLog::Emergency("pcom.exit.fix cleanup.begin entries=%lu poolFlags=%02X",
                Read32(g_pcom + kList + 12), g_client[kPoolFlags]);
            // The EXE CRT runs this callback BEFORE ExitProcess and PCOM detach.
            // Leave PCOM's later registered destructor intact; it sees an empty
            // table. Do not suppress exceptions or change ordinary Alloc/Free.
            reinterpret_cast<Destructor>(g_pcom + kListDestructor)();
            ClientLog::Emergency("pcom.exit.fix cleanup.end entries=%lu poolFlags=%02X",
                Read32(g_pcom + kList + 12), g_client[kPoolFlags]);
        }
    }
    g_destroyPool();
    if (destroying)
        ClientLog::Emergency("pcom.exit.fix pool.end poolFlags=%02X", g_client[kPoolFlags]);
}

bool Reject(const char* reason)
{
    CrashReporter::RecordEvent("pcom.exit.fix", "install.skip reason=%s", reason);
    return false;
}
}

bool PcomExitFix::Install(HMODULE clientModule, HMODULE pcomModule)
{
    auto* client = reinterpret_cast<BYTE*>(clientModule);
    auto* pcom = reinterpret_cast<BYTE*>(pcomModule);
    if (g_installed) return g_client == client && g_pcom == pcom;
    if (!MatchesImage(client, 0x4B7C15C9, 0xA94000)
        || !MatchesImage(pcom, 0x4B7C140C, 0x25000)) return Reject("image_mismatch");
    if (!MatchesCode(client, pcom)) return Reject("code_mismatch");
    if (!UsesNativePool(client, pcom)) return Reject("allocator_mismatch");
    if (client[kPoolFlags] & 1) return Reject("pool_already_destroyed");

    g_client = client;
    g_pcom = pcom;
    g_destroyPool = reinterpret_cast<Destructor>(client + kPoolDestructor);
    LONG result = DetourTransactionBegin();
    if (result == NO_ERROR) {
        result = DetourUpdateThread(GetCurrentThread());
        if (result == NO_ERROR)
            result = DetourAttach(reinterpret_cast<PVOID*>(&g_destroyPool), DestroyPoolAfterList);
        if (result == NO_ERROR) result = DetourTransactionCommit();
        else DetourTransactionAbort();
    }
    g_installed = result == NO_ERROR;
    if (!g_installed) {
        g_client = nullptr;
        g_pcom = nullptr;
        g_destroyPool = nullptr;
    }
    CrashReporter::RecordEvent("pcom.exit.fix", "install result=%d error=%ld client=%p pcom=%p",
        g_installed ? 1 : 0, result, clientModule, pcomModule);
    return g_installed;
}
