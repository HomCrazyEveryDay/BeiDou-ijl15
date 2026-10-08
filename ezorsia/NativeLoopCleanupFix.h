#pragma once
#include <windows.h>

namespace NativeLoopCleanupFix {
// Install before hooking CWvsApp::Run, independently of diagnostic settings.
bool Install(HMODULE client);
}
