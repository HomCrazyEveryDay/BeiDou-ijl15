#pragma once
#include <windows.h>

namespace TargetedCrashSnapshot {
constexpr ULONG StreamType = 0x42445331; // BDS1, local minidump user stream
enum class Family : DWORD { ActionJump, MobInitialization, NullVariant, Canvas, KernelWrite, MainLoopEscape, Count };
struct Memory {
    DWORD address = 0, size = 0, copied = 0;
    BYTE bytes[256]{};
};
struct Frame {
    DWORD address = 0;
    DWORD words[6]{}; // saved EBP, return address, four arguments
};
// Bounded POD: no allocation, raw data stays in the local dump user stream.
struct Snapshot {
    DWORD version = 1, size = sizeof(Snapshot), threadId = 0;
    Family family = Family::ActionJump;
    SYSTEMTIME time{};
    ULONGLONG tick = 0;
    EXCEPTION_RECORD record{};
    CONTEXT context{};
    MEMORY_BASIC_INFORMATION faultRegion{};
    DWORD frameCount = 0, memoryCount = 0;
    Frame frames[24]{};
    Memory memory[8]{};
};
using Capture = void(*)(const Snapshot&);
// Follows the existing crash-dump preference, independent of verbose tracing.
bool Install(HMODULE client, Capture capture);
const char* Name(Family family);
}
