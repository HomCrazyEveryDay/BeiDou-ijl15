#pragma once
#include <windows.h>

namespace PetPotionSettings {
bool Install(HMODULE client);
// CConfig::SaveGlobal also runs with an unavailable legacy registry handle.
void Save(void* config);
}
