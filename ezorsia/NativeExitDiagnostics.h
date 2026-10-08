#pragma once
#include <windows.h>
#include "TargetedCrashSnapshot.h"

namespace NativeExitDiagnostics {
using Capture = void(*)(const TargetedCrashSnapshot::Snapshot&, unsigned);
// Enabled with crash dumps, independently of verbose lifecycle/conditional dumps.
bool Install(HMODULE client, Capture capture);
}
