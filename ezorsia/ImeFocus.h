#pragma once

// Shared with custom text controls in new IME mode. The single owner and
// implementations live in FixIme.h, included only by Client.cpp.
extern "C" void __cdecl EnableImeForControl(void* control);
extern "C" void __cdecl DisableImeForControl(void* control);
