#pragma once
#include <cstddef>
#include <cstdint>

namespace DisconnectDiagnostics {
enum class ClosePath { Unknown, ConfirmedLogout, ProcessCleanup };
// EXE-relative return addresses, after validating the supported build's call sites.
inline ClosePath ClassifyClosePath(const std::uintptr_t* frames, std::size_t count) {
    bool confirm = false, returnLogin = false, ui = false, cleanup = false, mainCleanup = false;
    for (std::size_t i = 0; i < count; ++i) {
        const auto frame = frames[i];
        // Never suppress the native FD_CLOSE / network-error route.
        if (frame == 0x5FEB20 || frame == 0x096369) return ClosePath::Unknown;
        confirm |= frame == 0x6068BB;
        returnLogin |= frame == 0x62467F;
        ui |= frame == 0x607529 || frame == 0x4D42B0;
        cleanup |= frame == 0x5F5219;
        mainCleanup |= frame == 0x5F1CF1;
    }
    if (confirm && returnLogin && ui) return ClosePath::ConfirmedLogout;
    if (cleanup && mainCleanup) return ClosePath::ProcessCleanup;
    return ClosePath::Unknown;
}
}
