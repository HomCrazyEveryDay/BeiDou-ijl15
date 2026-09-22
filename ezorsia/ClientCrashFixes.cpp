#include "stdafx.h"
#include "ClientCrashFixes.h"

#include "CrashReporter.h"
#include "Memory.h"
#include <cstring>

namespace {

// Effect_Catch's success branch already plays a WzProperty frame sequence.
// The shipped BasicEff.img has a sequence at Catch/Fail too, whereas v83's
// failure branch queries IWzCanvas directly and passes null to InsertCanvas.
DWORD g_catchAnimationResume = 0x00438FDD;

__declspec(naked) void AnimatedCatchFailure()
{
	__asm {
		// Preserve EAX (the temporary string destination on the native stack).
		mov dword ptr [ebp - 54h], esp
		push 0E68h // StringPool: Catch/Fail; success uses 0E67h.
		jmp dword ptr [g_catchAnimationResume]
	}
}

bool InstallAnimatedCatchFailure()
{
	// Redirect only the zero/success=false branch. Its entry stack and the
	// success branch's SEH state are identical at this point (state 9).
	auto branch = reinterpret_cast<BYTE*>(0x00438FCF);
	const BYTE expected[] = {0x0F, 0x84, 0x63, 0x01, 0x00, 0x00};
	const BYTE resume[] = {0x50, 0xE8, 0x22, 0x58, 0x36, 0x00};
	if (std::memcmp(branch, expected, sizeof(expected)) != 0
		|| std::memcmp(reinterpret_cast<void*>(g_catchAnimationResume), resume, sizeof(resume)) != 0) {
		return false;
	}
	DWORD oldProtect = 0;
	if (!VirtualProtect(branch, sizeof(expected), PAGE_EXECUTE_READWRITE, &oldProtect)) return false;
	const DWORD displacement = reinterpret_cast<DWORD>(&AnimatedCatchFailure)
		- (reinterpret_cast<DWORD>(branch) + sizeof(expected));
	std::memcpy(branch + 2, &displacement, sizeof(displacement));
	FlushInstructionCache(GetCurrentProcess(), branch, sizeof(expected));
	DWORD ignored = 0;
	VirtualProtect(branch, sizeof(expected), oldProtect, &ignored);
	return true;
}

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
	CrashReporter::RecordEvent("magnet.animation", "install result=%d",
		InstallAnimatedCatchFailure() ? 1 : 0);
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
