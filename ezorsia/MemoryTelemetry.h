#pragma once

namespace MemoryTelemetry {
// Samples address space, commit and GDI/USER counts into ijl15-memory-*.log.
// On by default every 60 s; BEIDOU_MEMORY_TELEMETRY_SECONDS=0 disables it,
// other values are clamped to 10..3600. Independent of crash dump settings.
void Start();
}
