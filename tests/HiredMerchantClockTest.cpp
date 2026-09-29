#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../ezorsia/HiredMerchantClock.h"
#include "../ezorsia/ClientLog.h"

void ClientLog::Append(Component, const char*, ...) {}
constexpr DWORD Native(DWORD address) { return address + 0x20000000; }

// Execute the real installed cave and original division/guard instructions.
int Remaining(unsigned elapsedMinutes) {
    const DWORD address = Native(0x518343);
    int result;
    __asm {
        push ebx
        push esi
        mov eax, elapsedMinutes
        call address
        mov result, eax
        pop esi
        pop ebx
    }
    return result;
}

void Duration(unsigned hours) {
    unsigned char packet[] = {0,0,0,0,0x0d,0x10,1,
        static_cast<unsigned char>(hours), static_cast<unsigned char>(hours >> 8)};
    assert(HiredMerchantClock::HandlePacket(packet, sizeof(packet)));
}

int main() {
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    void* page = VirtualAlloc(reinterpret_cast<void*>(Native(0x510000)), 0x10000,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(page == reinterpret_cast<void*>(Native(0x510000)));
    const unsigned char code[] = {
        0xbb,0x9f,0x05,0,0,0x6a,0x3c,0x59,0x2b,0xd8,
        0x8b,0xc3,0x99,0xf7,0xf9,0x8b,0xf0,0x85,0xf6,
        0x0f,0x8c,0x99,0x01,0,0,0x83,0xfe,0x17,0x0f,0x8f,0x90,0x01,0,0,
        0x8b,0xc3,0xc3 // Return remaining minutes after passing the native hour guards.
    };
    auto entry = reinterpret_cast<unsigned char*>(Native(0x518343));
    std::memcpy(entry, code, sizeof(code));
    // Mismatched binaries must be left untouched.
    entry[0] = 0x90;
    assert(!HiredMerchantClock::Install());
    assert(entry[1] == code[1] && entry[25] == code[25]);
    entry[0] = code[0];
    DWORD old;
    assert(VirtualProtect(page, 0x10000, PAGE_EXECUTE_READ, &old));
    assert(HiredMerchantClock::Install());
    assert(HiredMerchantClock::Install());
    MEMORY_BASIC_INFORMATION info{};
    assert(VirtualQuery(entry, &info, sizeof(info)));
    assert(info.Protect == PAGE_EXECUTE_READ);

    assert(Remaining(0) == 1440);
    Duration(72);
    assert(Remaining(0) == 4320);
    assert(Remaining(1440) == 2880);
    assert(Remaining(4319) == 1);
    assert(Remaining(4320) == 0);
    assert(Remaining(5000) == 0);
    Duration(168);
    assert(Remaining(0) == 10080);
    assert(Remaining(4320) == 5760);
    assert(Remaining(10080) == 0);
    Duration(596);
    assert(Remaining(0) == 35760);
    Duration(597);
    Duration(0);
    assert(Remaining(0) == 35760);

    unsigned char packet[] = {0,0,0,0,0x0d,0x10,1,72,0,0};
    assert(!HiredMerchantClock::HandlePacket(nullptr, 9));
    for (unsigned n = 0; n < 6; ++n) assert(!HiredMerchantClock::HandlePacket(packet, n));
    for (unsigned n = 6; n < 9; ++n) assert(HiredMerchantClock::HandlePacket(packet, n));
    assert(HiredMerchantClock::HandlePacket(packet, 10));
    packet[6] = 2;
    assert(HiredMerchantClock::HandlePacket(packet, 9));
    assert(Remaining(0) == 35760);
    packet[4] = 0x7d; packet[5] = 0;
    assert(!HiredMerchantClock::HandlePacket(packet, 9));
    assert(Remaining(0) == 1440);
    puts("HiredMerchantClock: native x86 clock, boundaries, packet validation and page protection passed.");
}
