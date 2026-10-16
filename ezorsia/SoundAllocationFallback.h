#pragma once
#include <windows.h>

namespace SoundAllocationFallback {
// SOUND_DX8.DLL optionally predecodes a whole clip: +AC25 builds a temporary
// decoder, +AC40 -> +5B45 -> +19C0 allocates the buffer and +1A33 writes
// through HeapAlloc's result without a NULL check. The real buffer is now
// allocated before the decoder exists; on failure the module's existing
// non-predecoded branch (+ACA1) runs, which needs no cleanup. The allocator's
// HeapAlloc call (+1A2D) adopts that buffer instead of allocating again.
// PCOM loads the module lazily with LoadLibraryA; patching happens after that
// call returns, outside the loader lock.
bool Install();
#ifdef BEIDOU_SOUND_FALLBACK_TEST
// Test-only: redirect stub continuations so they are observable.
void RedirectForTest(DWORD resume, DWORD skip);
// Test-only: buffer allocated for the current thread and not yet adopted.
void* PendingForTest();
#endif
}
