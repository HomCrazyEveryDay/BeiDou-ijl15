#pragma once
#include <windows.h>

namespace ResourceLookupDiagnostics {
// Observation only. Follows the crash-dump preference; does not retry resources,
// change COM results, omit drawing, or handle native exceptions.
bool Install(HMODULE client, bool enabled);
}
