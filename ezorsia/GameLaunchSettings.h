#pragma once
#include <cstdint>
#include <cstring>

// Wire contract: keep identical to BeiDou-ijl15/ezorsia/GameLaunchSettings.h.
namespace game_settings
{
enum Field : unsigned {
    Width, Height, Ime, MovementKeys, CrashDump, CrashTrace, FullDump,
    StartupLog, BuffLog, EquipmentLog, LifecycleDiagnostics, ConditionalDump,
    ExitMonitor, TestEndpoint, Port, Count
};
struct Snapshot {
    std::uint32_t version = 1;
    std::uint32_t size = sizeof(Snapshot);
    std::uint32_t values[Count] = {1280, 720, 1, 0, 1, 1, 0, 0, 0, 1, 0, 0, 1, 0, 8484};
    char endpoint[64] = "127.0.0.1";
};
static_assert(sizeof(Snapshot) == 132, "Launch settings ABI changed");
inline bool Valid(const Snapshot& s) {
    if (s.version != 1 || s.size != sizeof(Snapshot)
        || s.values[Width] < 800 || s.values[Width] > 3840
        || s.values[Height] < 600 || s.values[Height] > 2160
        || s.values[Port] < 1 || s.values[Port] > 65535
        || std::memchr(s.endpoint, 0, sizeof(s.endpoint)) == nullptr) return false;
    for (unsigned i = Ime; i < Port; ++i) if (s.values[i] > 1) return false;
    return true;
}
}
