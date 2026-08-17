#include "stdafx.h"
#include "ClientCrashFixes.h"

#include "CrashReporter.h"
#include "Memory.h"

namespace {

// Character-selection UI creation handler. The single argument is the base
// address of 15 fixed-size (0x2AC-byte) character records.
using InitializeCharacterList = void(__thiscall*)(void* self, void* records);
InitializeCharacterList g_originalInitializeCharacterList =
	reinterpret_cast<InitializeCharacterList>(0x006042DC);

void __fastcall HookInitializeCharacterList(void* self, void*, void* records)
{
	if (records == nullptr) {
		// The native function immediately turns a null base into
		// 0, 0x2AC, 0x558, ... and dereferences the first entry while drawing.
		// Skip the incomplete UI initialization as one unit so later slots do not
		// receive those offset-derived invalid pointers.
		CrashReporter::RecordEvent(
			"charSelect.guard",
			"skip null character-list records self=%p",
			self);
		return;
	}

	g_originalInitializeCharacterList(self, records);
}

} // namespace

void ClientCrashFixes::Install()
{
	const bool installed = Memory::SetHook(
		true,
		reinterpret_cast<void**>(&g_originalInitializeCharacterList),
		HookInitializeCharacterList);
	CrashReporter::RecordEvent(
		"charSelect.guard",
		"install result=%d trampoline=%p",
		installed ? 1 : 0,
		reinterpret_cast<void*>(g_originalInitializeCharacterList));
}
