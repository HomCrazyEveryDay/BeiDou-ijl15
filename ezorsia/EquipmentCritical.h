#pragma once
#include <atomic>

namespace EquipmentCritical {
// One atomic snapshot; probability and damage always come from the same packet.
inline std::atomic<unsigned>& Snapshot() {
    static std::atomic<unsigned> value{0};
    return value;
}
inline bool Active() { return Snapshot().load() != 0; }
inline void Reset() { Snapshot().store(0); }

inline bool HandlePacket(const unsigned char* data, unsigned long size) {
    if (!data || size < 6) return false;
    const unsigned opcode = data[4] | (data[5] << 8);
    if (opcode == 0) Reset(); // fresh login, including changing accounts
    if (opcode != 0x1010) return false;
    if (size != 10 || data[6] != 1 || data[7] > 100) { Reset(); return true; }
    const unsigned damage = data[8] | (data[9] << 8);
    Snapshot().store(damage <= 1000 ? data[7] | (damage << 8) : 0);
    return true;
}

inline unsigned Footer(int count, const int* critical, unsigned original) {
    if (!Active() || count < 1 || count > 15) return original;
    unsigned result = 0xcc010000;
    for (int i = 0; i < count; ++i) if (critical[i]) result |= 1u << i;
    return result;
}

// Native physical damage stores a total percentage; magic stores a legacy
// value that subtracts 100 (<=200) or 200 (>200) at the critical branch.
inline void Add(int& probability, int& damage, bool magic) {
    const unsigned bonus = Snapshot().load();
    if (!bonus) return;
    const int addedProbability = bonus & 0xff;
    const int addedDamage = bonus >> 8;
    probability = probability > 100 - addedProbability ? 100 : probability + addedProbability;
    if (probability <= 0) return; // damage-only equipment cannot grant critical chance
    if (!magic) {
        if (damage <= 100) damage = 150;
        damage += addedDamage;
    } else {
        int extra = damage > 200 ? damage - 200 : damage - 100;
        if (damage <= 100) extra = 50;
        extra += addedDamage;
        damage = extra <= 100 ? extra + 100 : extra + 200;
    }
}

bool Install();
}
