#pragma once
#include <cstddef>
#include <cstdint>

namespace SkillPointSync {
inline unsigned Read16(const unsigned char* p) { return p[0] | (unsigned(p[1]) << 8); }
inline unsigned Read32(const unsigned char* p) { return Read16(p) | (Read16(p + 2) << 16); }
inline int Stage(int job) {
    if (job == 2200) return 1;
    if (job >= 2210 && job <= 2218) return job - 2208;
    const int family = job / 100;
    if (!((family >= 1 && family <= 5) || (family >= 11 && family <= 15) || family == 21)) return 0;
    if (job % 100 == 0) return 1;
    return job % 10 <= 2 ? job % 10 + 2 : 0;
}

struct State {
    unsigned character = 0;
    int job = 0;
    bool ready = false;
    unsigned available[10]{};

    // Observe only verified native identity fields; never change the packet cursor.
    void Observe(const unsigned char* data, std::size_t size) {
        if (!data || size < 6) return;
        const unsigned opcode = Read16(data + 4);
        if (opcode == 0x7d && size >= 12 && data[11] == 1) {
            *this = {};
            if (size < 89 || data[12] || data[13] || data[34]) return;
            for (unsigned i = 26; i < 34; ++i) if (data[i] != 0xff) return;
            character = Read32(data + 35);
            job = Read16(data + 87);
        } else if (opcode == 0x1f && size >= 11) {
            unsigned mask = Read32(data + 7), pos = 11;
            if (!(mask & 0x20) || (mask & 8)) return;
            for (unsigned bit = 1; bit <= 0x20; bit <<= 1) {
                if (!(mask & bit)) continue;
                const unsigned width = (bit == 1 || bit == 0x10) ? 1 : (bit <= 4 ? 4 : 2);
                if (size - pos < width) return;
                if (bit == 0x20) { job = Read16(data + pos); ready = false; }
                pos += width;
            }
        }
    }

    bool Receive(const unsigned char* data, std::size_t size) {
        if (!data || size < 6 || Read16(data + 4) != 0x100c) return false;
        // Consume even an invalid extension, so it cannot reach the native dispatcher.
        ready = false;
        const bool evan = job == 2200 || (job >= 2210 && job <= 2218);
        const unsigned count = evan ? 10 : 4, width = evan ? 4 : 2;
        if (size != 13 + count * width || data[6] != (evan ? 2 : 1) || !character || Read32(data + 7) != character
                || Read16(data + 11) != job || !Stage(job)) return true;
        unsigned previous = evan ? 0x7fffffff : 0x7fff;
        for (unsigned i = 0; i < count; ++i) {
            const unsigned value = evan ? Read32(data + 13 + i * width) : Read16(data + 13 + i * width);
            if (value > previous || (i >= unsigned(Stage(job)) && value)) return true;
            available[i] = value;
            previous = value;
        }
        ready = true;
        return true;
    }

    int ForStage(int stage, int fallback) const {
        return ready && stage > 0 && stage <= Stage(job) ? int(available[stage - 1]) : fallback;
    }
};
}
