#pragma once

namespace ModuleAudit {
// Call once at the process entry hook, outside DllMain/loader lock.
// Observation only: never blocks a module or changes gameplay.
void Start();
#ifdef MODULE_AUDIT_TEST
void StopForTest();
void FailSnapshotsForTest(bool fail);
void ExerciseBoundsForTest();
#endif
}
