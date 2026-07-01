#include "stdafx.h"
#include "NpcShopCurrency.h"

namespace
{
const DWORD kLoadCostItemPatch = 0x007566F3;
const DWORD kLoadCostItemRetn = 0x007566F8;
const DWORD kPushCostItemPatch = 0x007568AC;
const DWORD kPushCostItemRetn = 0x007568B1;
const DWORD kSkipPitchExtraInfoPatch = 0x00756820;
const DWORD kListCostIconPatch = 0x007555A7;
const DWORD kListCostIconRetn = 0x007555AD;
const DWORD kDrawListCostIconPatch = 0x007555C8;
const DWORD kDrawListCostIconRetn = 0x007555EF;
const DWORD kItemInfo = 0x00BE78D8;
const DWORD kGetItemIconCanvas = 0x005D3BD8;
const DWORD kPerfectPitchItemId = 0x0041C3F0;
const DWORD kHResultUnexpected = 0x8000FFFF;

DWORD g_cachedCostItemId = 0;
DWORD g_cachedCostIcon = 0;
DWORD g_currentListCostItemId = 0;
DWORD g_currentFallbackIcon = 0;

struct CanvasVariant
{
	DWORD data[4];
};

bool TryReadDword(DWORD address, DWORD* value)
{
	if (!address || !value)
	{
		return false;
	}

	__try
	{
		*value = *reinterpret_cast<DWORD*>(address);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		*value = 0;
		return false;
	}
}

DWORD ReadDwordOrZero(DWORD address)
{
	DWORD value = 0;
	TryReadDword(address, &value);
	return value;
}

DWORD LoadItemIconCanvas(DWORD itemId)
{
	DWORD itemInfo = ReadDwordOrZero(kItemInfo);
	if (!itemInfo)
	{
		return 0;
	}

	DWORD icon = 0;
	typedef DWORD*(__thiscall* GetItemIconCanvasFunc)(DWORD, DWORD*, DWORD, int, DWORD);
	__try
	{
		reinterpret_cast<GetItemIconCanvasFunc>(kGetItemIconCanvas)(itemInfo, &icon, itemId, 0, 0);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		icon = 0;
	}

	return icon;
}

DWORD GetOrLoadCostIcon(DWORD costItemId, DWORD fallbackIcon)
{
	if (!costItemId)
	{
		return fallbackIcon;
	}

	if (g_cachedCostItemId != costItemId || !g_cachedCostIcon)
	{
		g_cachedCostItemId = costItemId;
		g_cachedCostIcon = LoadItemIconCanvas(costItemId);
	}

	return g_cachedCostIcon ? g_cachedCostIcon : fallbackIcon;
}

DWORD __stdcall GetListCostIcon(DWORD shopWindow, DWORD shopItem)
{
	DWORD fallbackIcon = 0;
	__try
	{
		fallbackIcon = ReadDwordOrZero(shopWindow + 0xEC);
		g_currentFallbackIcon = fallbackIcon;
		DWORD costItemId = ReadDwordOrZero(shopItem + 0x20);
		g_currentListCostItemId = costItemId;
		return GetOrLoadCostIcon(costItemId, fallbackIcon);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		g_currentListCostItemId = 0;
		g_currentFallbackIcon = fallbackIcon;
		return fallbackIcon;
	}
}

DWORD CallCanvasDraw(DWORD destCanvas, DWORD sourceCanvas, int x, int y, const CanvasVariant& alpha)
{
	if (!destCanvas || !sourceCanvas)
	{
		return kHResultUnexpected;
	}

	DWORD vtable = ReadDwordOrZero(destCanvas);
	DWORD draw = ReadDwordOrZero(vtable + 0x80);
	if (!draw)
	{
		return kHResultUnexpected;
	}

	typedef DWORD(__stdcall* CanvasDrawFunc)(DWORD, int, int, DWORD, CanvasVariant);
	__try
	{
		return reinterpret_cast<CanvasDrawFunc>(draw)(destCanvas, x, y, sourceCanvas, alpha);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return kHResultUnexpected;
	}
}

DWORD CallCanvasCopyScaled(DWORD destCanvas, DWORD sourceCanvas, int x, int y, int width, int height)
{
	if (!destCanvas || !sourceCanvas)
	{
		return kHResultUnexpected;
	}

	DWORD vtable = ReadDwordOrZero(destCanvas);
	DWORD copy = ReadDwordOrZero(vtable + 0x84);
	if (!copy)
	{
		return kHResultUnexpected;
	}

	CanvasVariant empty = {};
	typedef DWORD(__stdcall* CanvasCopyFunc)(DWORD, int, int, DWORD, int, int, int, int, int, int, int, CanvasVariant);
	__try
	{
		return reinterpret_cast<CanvasCopyFunc>(copy)(destCanvas, x, y, sourceCanvas, 0xFF, width, height, 0, 0, 0, 0, empty);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return kHResultUnexpected;
	}
}

DWORD __stdcall DrawListCostIcon(DWORD destCanvas, DWORD sourceCanvas, int x, int y, CanvasVariant* alpha)
{
	CanvasVariant drawAlpha = {};
	if (alpha)
	{
		drawAlpha = *alpha;
	}

	if (!g_currentListCostItemId)
	{
		return CallCanvasDraw(destCanvas, sourceCanvas, x, y, drawAlpha);
	}

	DWORD result = CallCanvasCopyScaled(destCanvas, sourceCanvas, x, y, 12, 12);
	if (static_cast<LONG>(result) >= 0)
	{
		return result;
	}

	DWORD fallbackIcon = g_currentFallbackIcon ? g_currentFallbackIcon : sourceCanvas;
	return CallCanvasDraw(destCanvas, fallbackIcon, x, y, drawAlpha);
}

__declspec(naked) void LoadShopCostItemId()
{
	__asm
	{
		mov edi, dword ptr [esi + 20h]
		test edi, edi
		jne loaded
		mov edi, dword ptr [kPerfectPitchItemId]
	loaded:
		jmp dword ptr [kLoadCostItemRetn]
	}
}

__declspec(naked) void PushShopCostItemId()
{
	__asm
	{
		mov edx, dword ptr [esi + 20h]
		test edx, edx
		jne loaded
		mov edx, dword ptr [kPerfectPitchItemId]
	loaded:
		push edx
		jmp dword ptr [kPushCostItemRetn]
	}
}

__declspec(naked) void LoadListCostIconCave()
{
	__asm
	{
		pushfd
		push ecx
		push edx
		push esi
		push edi
		call GetListCostIcon
		pop edx
		pop ecx
		popfd
		jmp dword ptr [kListCostIconRetn]
	}
}

__declspec(naked) void DrawListCostIconCave()
{
	__asm
	{
		push ecx
		push edx
		mov eax, dword ptr [ebp + 8]
		mov dword ptr [ebp - 24h], eax
		lea edx, [ebp - 138h]
		push edx
		mov edx, dword ptr [ebp - 1Ch]
		dec edx
		push edx
		push 31h
		push dword ptr [ebp - 14h]
		push eax
		call DrawListCostIcon
		pop edx
		pop ecx
		mov esi, ebp
		sub esi, 128h
		mov edi, dword ptr [ebp - 1Ch]
		jmp dword ptr [kDrawListCostIconRetn]
	}
}

}

void NpcShopCurrency::Install()
{
	Memory::CodeCave(LoadShopCostItemId, kLoadCostItemPatch, 5);
	Memory::CodeCave(PushShopCostItemId, kPushCostItemPatch, 5);
	Memory::CodeCave(LoadListCostIconCave, kListCostIconPatch, 6);
	Memory::CodeCave(DrawListCostIconCave, kDrawListCostIconPatch, 5);

	unsigned char skipPitchExtraInfo[] = { 0xEB, 0x4F, 0x90, 0x90, 0x90 };
	Memory::WriteByteArray(kSkipPitchExtraInfoPatch, skipPitchExtraInfo, sizeof(skipPitchExtraInfo));
}
