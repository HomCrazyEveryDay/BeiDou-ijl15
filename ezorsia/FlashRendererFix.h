#pragma once
#include <windows.h>
namespace FlashRendererFix {
// Keeps the hooked Gr2D module loaded for the lifetime of ijl15.
bool Install(HMODULE gr2d);
void Install();
}
