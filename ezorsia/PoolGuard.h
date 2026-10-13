#pragma once
#include "PoolGuardCore.h"

// Integrity checks for the native ZAllocEx pools that hold COM objects, BSTRs
// and ZXString data (anonymous 0xBF0B00, char 0xBF0A90, wide 0xBF0BA8).
// Detects double frees, writes into freed blocks and broken freelist links at
// the allocator, records the detecting caller (not necessarily the writer), and keeps the corrupt
// block out of circulation (skip the free or abandon that bucket's freelist).
namespace PoolGuard {
// Startup-only opt-in; no runtime enable/disable while allocator threads run.
// client is the mapped BeiDou.exe base. dumps enables at most two minidumps.
bool Install(HMODULE client, bool dumps);
struct Counters { LONG doubleFree, freedBlockWritten, freeListLinkBad, headerMismatch, reports; };
Counters Snapshot();
}
