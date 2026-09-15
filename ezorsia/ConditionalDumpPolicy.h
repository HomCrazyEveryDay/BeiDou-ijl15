#pragma once
#include <cstddef>
#include <cstdint>
namespace ConditionalDumpPolicy {
// Verified 083 IWzResMan::GetObject HRESULT failure, also reached by timed effects.
inline bool ResourceFailure(std::uint32_t code, const std::uintptr_t* frames,
    std::size_t count, std::uintptr_t base) {
    if (code != 0xE06D7363) return false;
    for (std::size_t i=0;i<count;++i) if (frames[i] == base + 0x3AF7) return true;
    return false;
}
inline bool Matches(std::uint32_t code, std::uint64_t skillTick, std::uint64_t now,
    const std::uintptr_t* frames, std::size_t count, std::uintptr_t base) {
    if (code != 0xE06D7363 || !skillTick || now < skillTick || now - skillTick > 5000) return false;
    for (std::size_t i=0;i<count;++i) if(frames[i] == base + 0xFECA9) return true;
    return false;
}
}
