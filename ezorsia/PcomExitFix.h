#pragma once
#include <windows.h>

namespace PcomExitFix {
// Called after PCOM has been initialized and pinned for the process lifetime.
bool Install(HMODULE client, HMODULE pcom);
}
