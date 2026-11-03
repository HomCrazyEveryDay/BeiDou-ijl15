#pragma once
#include <windows.h>

namespace SoundExitFix {
// Install after SOUND_DX8 has loaded, outside the loader lock.
bool Install(HMODULE sound);
}
