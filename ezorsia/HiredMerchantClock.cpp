#include "stdafx.h"
#include "HiredMerchantClock.h"
#include "ClientLog.h"
#include <cstring>

namespace {
#ifdef HIRED_MERCHANT_CLOCK_TEST
constexpr DWORD Native(DWORD address) { return address + 0x20000000; }
#else
constexpr DWORD Native(DWORD address) { return address; }
#endif
// Old servers send no extension: preserve their native 24-hour clock.
volatile LONG durationMinutes = 1440;
DWORD clockReturn = Native(0x0051834D);
bool installed = false;

// Native DrawClock: EAX = (elapsedMs + GetTickCount() - receiveTick) / 60000.
// Preserve its formatting/drawing path and replace only the remaining minutes.
__declspec(naked) void RemainingMinutes() {
    __asm {
        mov ebx, dword ptr [durationMinutes]
        sub ebx, eax
        jns nonnegative
        xor ebx, ebx
    nonnegative:
        push 3Ch
        pop ecx
        jmp dword ptr [clockReturn]
    }
}

unsigned Read16(const unsigned char* p) { return p[0] | (unsigned(p[1]) << 8); }
}

bool HiredMerchantClock::HandlePacket(const unsigned char* data, unsigned long size) {
    if (!data || size < 6) return false;
    const unsigned opcode = Read16(data + 4);
    if (opcode == 0x7d) InterlockedExchange(&durationMinutes, 1440);
    if (opcode != 0x100d) return false;
    // Consume malformed extension packets without passing them to native dispatch.
    if (size == 9 && data[6] == 1) {
        const unsigned hours = Read16(data + 7);
        if (hours >= 1 && hours <= 596) InterlockedExchange(&durationMinutes, hours * 60);
    }
    return true;
}

bool HiredMerchantClock::Install() {
    if (installed) return true;
    const BYTE expectedClock[] = {0xbb,0x9f,0x05,0,0,0x6a,0x3c,0x59,0x2b,0xd8};
    const BYTE expectedHours[] = {0x83,0xfe,0x17,0x0f,0x8f,0x90,0x01,0,0};
    auto clock = reinterpret_cast<BYTE*>(Native(0x00518343));
    auto hours = reinterpret_cast<BYTE*>(Native(0x0051835C));
    if (std::memcmp(clock, expectedClock, sizeof(expectedClock)) ||
        std::memcmp(hours, expectedHours, sizeof(expectedHours))) {
        ClientLog::Append(ClientLog::Component::Trace, "hired_merchant_clock install=signature_mismatch");
        return false;
    }
    // Both sites are in the same page; save and restore protection just once.
    DWORD oldProtection;
    constexpr unsigned span = 0x518365 - 0x518343;
    if (!VirtualProtect(clock, span, PAGE_EXECUTE_READWRITE, &oldProtection)) return false;
    std::memset(clock, 0x90, sizeof(expectedClock));
    clock[0] = 0xe9;
    const DWORD relative = reinterpret_cast<DWORD>(RemainingMinutes) - (Native(0x00518343) + 5);
    std::memcpy(clock + 1, &relative, sizeof(relative));
    // Remove only the upper bound of 23 hours. The negative guard is retained.
    std::memset(hours, 0x90, sizeof(expectedHours));
    FlushInstructionCache(GetCurrentProcess(), clock, span);
    DWORD ignored;
    const bool restored = VirtualProtect(clock, span, oldProtection, &ignored) != FALSE;
    installed = true;
    ClientLog::Append(ClientLog::Component::Trace, "hired_merchant_clock install=ok protection_restored=%d", restored);
    return true;
}
