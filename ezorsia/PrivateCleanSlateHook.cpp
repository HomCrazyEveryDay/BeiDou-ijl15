#include "stdafx.h"
#include "PrivateCleanSlateHook.h"

namespace
{
const DWORD kSpecialItemCheckPatch = 0x004F4E6C;
const DWORD kSpecialItemCheckContinue = 0x004F4E73;
const DWORD kSpecialItemAccepted = 0x004F4EE1;
const DWORD kScrollCategoryCheckPatch = 0x004F54D2;
const DWORD kScrollCategoryCheckContinue = 0x004F54DA;
const DWORD kScrollCategoryAccepted = 0x004F54F2;
const DWORD kGetEnchantModeAddr = 0x007F63E8;
const DWORD kSetEnchantResultAddr = 0x007F651C;

DWORD g_specialItemCheckContinue = kSpecialItemCheckContinue;
DWORD g_specialItemAccepted = kSpecialItemAccepted;
DWORD g_scrollCategoryCheckContinue = kScrollCategoryCheckContinue;
DWORD g_scrollCategoryAccepted = kScrollCategoryAccepted;
bool g_legendaryCleanSlatePending = false;

bool IsCleanSlate(int itemId)
{
	return (itemId >= 2049000 && itemId <= 2049003)
		|| (itemId >= 2049995 && itemId <= 2049999);
}

void __stdcall RecordScrollAttempt(int itemId, int legendarySpirit)
{
	g_legendaryCleanSlatePending = legendarySpirit != 0 && IsCleanSlate(itemId);
}

using GetEnchantMode_t = int(__fastcall*)(void* pThis, void* edx);
GetEnchantMode_t g_getEnchantMode = reinterpret_cast<GetEnchantMode_t>(kGetEnchantModeAddr);

int __fastcall GetEnchantModeHook(void* pThis, void* edx)
{
	const int mode = g_getEnchantMode(pThis, edx);
	if (!g_legendaryCleanSlatePending)
	{
		return mode;
	}

	g_legendaryCleanSlatePending = false;
	return mode == 2 ? 1 : mode;
}

using SetEnchantResult_t = void(__fastcall*)(void* pThis, void* edx, int success, int cursed);
SetEnchantResult_t g_setEnchantResult = reinterpret_cast<SetEnchantResult_t>(kSetEnchantResultAddr);

void __fastcall SetEnchantResultHook(void* pThis, void* edx, int success, int cursed)
{
	if (success == -1)
	{
		g_legendaryCleanSlatePending = false;
	}
	g_setEnchantResult(pThis, edx, success, cursed);
}

__declspec(naked) void AllowPrivateCleanSlateSpecialItem()
{
	__asm {
		// EBP+10 is nonzero when the request comes from the Legendary Spirit UI.
		push eax
		push dword ptr[ebp + 10h]
		push eax
		call RecordScrollAttempt
		pop eax

		cmp eax, 2049995
		jb nativeCheck
		cmp eax, 2049999
		jbe accepted

	nativeCheck:
		// Replay the original 2040727 comparison overwritten at 004F4E6C.
		cmp eax, 2040727
		je accepted
		jmp dword ptr[g_specialItemCheckContinue]

	accepted:
		jmp dword ptr[g_specialItemAccepted]
	}
}

__declspec(naked) void AllowPrivateCleanSlateScrollCategory()
{
	__asm {
		// Preserve the native 20490 category and allow only BeiDou's reserved IDs.
		cmp esi, 20490
		je accepted
		cmp edi, 2049995
		jb nativeContinue
		cmp edi, 2049999
		jbe accepted

	nativeContinue:
		jmp dword ptr[g_scrollCategoryCheckContinue]

	accepted:
		jmp dword ptr[g_scrollCategoryAccepted]
	}
}
}

namespace PrivateCleanSlateHook
{
void Install()
{
	Memory::CodeCave(AllowPrivateCleanSlateSpecialItem, kSpecialItemCheckPatch, 7);
	Memory::CodeCave(AllowPrivateCleanSlateScrollCategory, kScrollCategoryCheckPatch, 8);
	Memory::SetHook(true, reinterpret_cast<void**>(&g_getEnchantMode), GetEnchantModeHook);
	Memory::SetHook(true, reinterpret_cast<void**>(&g_setEnchantResult), SetEnchantResultHook);
}
}
