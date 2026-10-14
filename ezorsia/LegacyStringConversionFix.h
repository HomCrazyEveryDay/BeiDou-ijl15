#pragma once
#include <windows.h>

namespace LegacyStringConversionFix {
// v83 and its WZ components use 0x3fffffff for an already-sized buffer.
// Install at startup, independently of crash diagnostics.
bool Install(HMODULE client);
}
