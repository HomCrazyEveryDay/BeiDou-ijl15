#pragma once
#include <windows.h>

namespace TargetedCrashSnapshot {
constexpr ULONG StreamType = 0x42445331; // BDS1, local minidump user stream
// Append only: numeric values are persisted in BDS1 streams and filenames.
enum class Family : DWORD { ActionJump, MobInitialization, NullVariant, Canvas, KernelWrite, MainLoopEscape, PcomFactory, BossResource, CashCategory, FaceResource, Count };
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
// Called only for RTTI-verified _com_error by the main-loop exception observer.
// Saves the FIRST throw, before native window-procedure translation/unwinding.
void ObserveResourceCpp(EXCEPTION_POINTERS* info, DWORD hresult, void* const* frames, unsigned count);
// Called only for RTTI-verified CTerminateException. Capture the native cash
// category mismatch before its commodity and category locals are unwound.
void ObserveCashCategoryCpp(EXCEPTION_POINTERS* info, DWORD code);
const char* Name(Family family);
}
