#include <windows.h>
#include <cassert>
#include <cstdio>
#include <vector>
#include "../ezorsia/EvanSpDiagnostics.h"

void ClientLog::Append(Component, const char*, ...) {}
static void Put(std::vector<unsigned char>& p, unsigned at, unsigned value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) p[at + i] = static_cast<unsigned char>(value >> (8 * i));
}
int main() {
    using namespace EvanSpDiagnostics;
    int job = 2218;
    // A one-book SP update is only 14 bytes: the previous 26-byte gate lost it.
    std::vector<unsigned char> p(14);
    Put(p, 4, 0x1f, 2); Put(p, 7, 0x8000, 4);
    p[11] = 1; p[12] = 10; p[13] = 11;
    auto s = Decode(p.data(), static_cast<unsigned long>(p.size()), job);
    assert(s.decoded && s.sp[8] == 0 && s.sp[9] == 11);
    for (unsigned n = 0; n < p.size(); ++n) assert(!Decode(p.data(), n, job).decoded);
    p[11] = 0;
    s = Decode(p.data(), 12, job); assert(s.decoded && s.sp[9] == 0);
    p[11] = 11; assert(!Decode(p.data(), 14, job).decoded);
    p[11] = 1; p[12] = 0; assert(!Decode(p.data(), 14, job).decoded);
    p[12] = 11; assert(!Decode(p.data(), 14, job).decoded);
    p[12] = 10;
    job = 112; assert(!Decode(p.data(), 14, job).relevant);
    job = -1; assert(!Decode(p.data(), 14, job).decoded);
    job = 2218; Put(p, 7, 0x400, 4);
    assert(!Decode(p.data(), 13, job).relevant); // ordinary HP update

    // Mixed scalar stats precede the variable SP table; JOB updates its decoder.
    p.assign(26, 0); Put(p, 4, 0x1f, 2); Put(p, 7, 0xC431, 4);
    p[11] = 0; p[12] = 160; Put(p, 13, 2218, 2);
    Put(p, 15, 1000, 2); Put(p, 17, 5, 2);
    p[19] = 3; p[20] = 1; p[21] = 11; p[22] = 9; p[23] = 11; p[24] = 10; p[25] = 11;
    job = 2217; s = Decode(p.data(), static_cast<unsigned long>(p.size()), job);
    assert(job == 2218 && s.decoded && s.sp[0] == 11 && s.sp[8] == 11 && s.sp[9] == 11);
    p[22] = 1; assert(!Decode(p.data(), static_cast<unsigned long>(p.size()), job).decoded);

    // Full login field, including the stat prefix; no unrelated data is logged.
    p.assign(110, 0); Put(p, 4, 0x7d, 2); p[10] = 1; p[11] = 1;
    for (unsigned i = 26; i < 34; ++i) p[i] = 0xff;
    Put(p, 87, 2218, 2); p[107] = 1; p[108] = 10; p[109] = 11;
    job = -1; s = Decode(p.data(), static_cast<unsigned long>(p.size()), job);
    assert(job == 2218 && s.decoded && s.sp[8] == 0 && s.sp[9] == 11);
    for (unsigned n = 0; n < p.size(); ++n) assert(!Decode(p.data(), n, job).decoded);
    p[11] = 0; assert(!Decode(p.data(), static_cast<unsigned long>(p.size()), job).relevant);
    p[11] = 1; p[26] = 0; assert(!Decode(p.data(), static_cast<unsigned long>(p.size()), job).decoded);
    puts("PASS Evan SP diagnostics: short packets, mixed stats, zero/invalid books, login, bounds, non-Evan");
}
