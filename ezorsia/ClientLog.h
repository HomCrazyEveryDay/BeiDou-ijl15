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
}
