#pragma once

#include <windows.h>

namespace ClientLog {
enum class Component { Startup, Verify, Authorization, Trace, Equipment, BuffIcons, Lifecycle, Count };

void Initialize();
const wchar_t* Directory();
const wchar_t* SessionId();
HANDLE Open(Component component);
void Write(HANDLE file, const char* text);
void Append(Component component, const char* format, ...);
// Minimal crash/exit evidence: bypass normal logger locks and flush immediately.
void Emergency(const char* format, ...);
// One non-fatal observation: reuse a thread-local handle and flush once at exit.
// Ordinary fatal/exit logging outside this scope still flushes immediately.
class EmergencyBatch {
public:
    EmergencyBatch();
    ~EmergencyBatch();
    EmergencyBatch(const EmergencyBatch&) = delete;
    EmergencyBatch& operator=(const EmergencyBatch&) = delete;
};
}
